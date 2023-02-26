
#include <core/OutputStationary.h>

namespace core {

    /* CYCLES */

    template <typename T>
    void OutputStationary<T>::fill_window_steps(std::vector<std::vector<int>> &window_steps,
            uint32_t window_out_size, uint32_t channels) {

        const std::vector<size_t> &act_shape = this->act->getShape();
        const std::vector<size_t> &wgt_shape = this->wgt->getShape();

        auto num_windows = this->out_x * this->out_y;
        auto Nx = act_shape[2];
        auto Ny = act_shape[3];

        auto Kx = wgt_shape[2];
        auto Ky = wgt_shape[3];

        uint64_t tmp_size = 0;
        std::vector<int> tmp_window_steps;
        auto position_on_chip = std::vector<std::vector<bool>>(Ny, std::vector<bool>(Nx, false));

        int ws = 0;
        while (ws < window_sets) {

            auto set_size = 0;
            std::set<std::tuple<int, int>> new_positions;

            auto start_window = ws * this->EF_COLUMNS;
            auto total_windows = std::min(this->EF_COLUMNS, (uint32_t)(num_windows - start_window));
            auto end_window = start_window + total_windows;

            for (int w = start_window; w < end_window; ++w) {
                auto x_window = (w % out_x) * this->stride;
                auto y_window = (w / out_x) * this->stride;

                for (int y = 0; y < Ky; y++) {
                    for (int x = 0; x < Kx; x++) {
                        if (position_on_chip[x_window + x][y_window + y])
                            continue;

                        new_positions.emplace(std::make_tuple(x_window + x, y_window + y));
                    }
                }

                set_size += window_out_size;
            }

            set_size += ceil(new_positions.size() * channels * this->dram->getActDataSize() / 8.);

            if (tmp_size + set_size > this->gbuffer->getActSize()) {
                assert(!tmp_window_steps.empty());
                position_on_chip = std::vector<std::vector<bool>>(Ny, std::vector<bool>(Nx, false));
                window_steps.emplace_back(tmp_window_steps);
                tmp_window_steps.clear();
                tmp_size = 0;
                continue;
            }

            tmp_size += set_size;
            for (const auto &pos : new_positions)
                position_on_chip[std::get<0>(pos)][std::get<1>(pos)] = true;
            tmp_window_steps.push_back(ws++);
        }

        if (tmp_size != 0) {
            assert(!tmp_window_steps.empty());
            window_steps.emplace_back(tmp_window_steps);
        }

    }

    template <typename T>
    std::vector<AddressRange> OutputStationary<T>::generate_addresses(uint32_t start_act_blk, uint32_t end_act_blk,
            uint32_t last_act_blk, uint32_t start_window, uint32_t end_window, uint32_t start_group) {

        const std::vector<size_t> &wgt_shape = this->wgt->getShape();

        auto Kx = wgt_shape[2];
        auto Ky = wgt_shape[3];

        auto start_y = start_act_blk / (Kx * last_act_blk);
        auto start_rem = start_act_blk % (Kx * last_act_blk);
        auto start_x = start_rem / last_act_blk;
        auto start_ch = start_rem % last_act_blk;

        auto window_blks = end_act_blk - start_act_blk;
        auto total_windows = end_window - start_window;
        auto read_addresses = std::vector<uint64_t>(total_windows * window_blks, NULL_ADDR);
        for (int w = 0; w < total_windows; ++w) {
            auto window = start_window + w;
            auto x_window = (window % out_x) * this->stride;
            auto y_window = (window / out_x) * this->stride;

            auto y = start_y;
            auto x = start_x;
            auto ch = start_ch;

            int idx = 0;
            while (y < Ky && idx != window_blks) {
                while (x < Kx && idx != window_blks) {
                    while (ch < last_act_blk && idx != window_blks) {
                        read_addresses[w * window_blks + idx] =
                                act_address_map[y_window + y][x_window + x][start_group + ch];
                        idx++;
                        ch++;
                    }
                    ch = 0;
                    x++;
                }
                x = 0;
                y++;
            }
        }

        return this->dram->compress_addresses(read_addresses);
    }

    template <typename T>
    void OutputStationary<T>::generate_memory_maps() {

        const std::vector<size_t> &act_shape = this->act->getShape();

        uint64_t act_channels, Nx, Ny;
        if (this->_3dim) {
            act_channels = act_shape[2];
            Nx = 1;
            Ny = 1;
        } else {
            act_channels = act_shape[1];
            Nx = act_shape[2];
            Ny = act_shape[3];
        }

        // Generate address map
        auto channel_blks = ceil(act_channels / (double)this->dram->getActValuesPerBlock());
        act_address_map = std::vector<std::vector<std::vector<uint64_t>>>(Ny, std::vector<std::vector<uint64_t>>(Nx,
                std::vector<uint64_t>(channel_blks)));

        // Column third
        for (int y = 0; y < Ny; ++y) {

            // Row second
            for (int x = 0; x < Nx; ++x) {

                // Store channel-first
                for (int k = 0; k < channel_blks; ++k) {
                    act_address_map[y][x][k] = this->dram->getStartActAddress() + next_act_address;
                    next_act_address += this->dram->getWidth();
                }
            }
        }

        act_bank_map = ActBankMap(Ny, std::vector<int>(Nx));

        int bank = 0;
        int bkp_bank = 0;
        for (int y = 0; y < Ny; ++y) {
            for (int x = 0; x < Nx; ++x) {
                if (y % this->stride == 0 && x == 0)
                    bank = bkp_bank;

                act_bank_map[y][x] = bank;
                bank = (bank + 1) % this->gbuffer->getActBanks();

                if (y % this->stride == 0 && x == out_x * this->stride - 1)
                    bkp_bank = bank;
            }
        }

    }

    template <typename T>
    void OutputStationary<T>::fill_weight_buffer() {

        // Data buffer
        weight_buffer = Buffer<T>(filter_sets * groups, BufferSet<T>(max_buffer_time,
                BufferRow<T>(this->EF_ROWS * this->EF_LANES, {0, 0, 0})));

        const std::vector<size_t> &wgt_shape = this->wgt->getShape();

        auto num_filters = wgt_shape[0];
        auto wgt_channels = wgt_shape[1];
        auto Kx = wgt_shape[2];
        auto Ky = wgt_shape[3];

        int set_wgt = -1;
        for (int g = 0; g < groups; ++g) {

            for (int m = 0; m < filters_per_group; ++m) {

                auto start_group = filters_per_group * g;

                if ((start_group + m) >= num_filters)
                    continue;

                auto filter_pos = m % this->EF_ROWS;
                if (filter_pos == 0)
                    set_wgt++;

                int buffer_time = 0;
                for (int y = 0; y < Ky; ++y) {
                    for (int x = 0; x < Kx; ++x) {
                        for (int k = 0; k < wgt_channels; k += this->EF_LANES) {
                            int index = 0;
                            for (int ch = k; ch < std::min((uint64_t) k + this->EF_LANES, wgt_channels); ++ch) {

                                index = depthwise ? filter_pos : index;
                                auto wgt_bits = this->wgt->get(start_group + m, ch, x, y);
                                int pos = filter_pos * this->EF_LANES + index;
                                weight_buffer[set_wgt][buffer_time][pos] = {wgt_bits, buffer_time, index};

                                index++;
                                if (index == this->EF_LANES) {
                                    buffer_time++;
                                    index = 0;
                                }
                            } // Channels

                            if (index != 0)
                                buffer_time++;

                        } // Channel sets
                    } // Kernel Width
                } // Kernel Height

            } // Filter sets
        } // Groups

        // BitTactical schedule
        if (this->arch->schedule()) {
            this->scheduler->schedule(weight_buffer, this->EF_LANES);
        }

        // Addresses buffer
        auto accesses_per_filter = (uint64_t)ceil(this->EF_LANES / (double)this->dram->getWgtValuesPerBlock())
                * this->EF_ROWS;//har lane ba har bar dastresi be bafer 4(wgt value per block) ta wgt ra mikhanad
        if (this->arch->schedule()) accesses_per_filter += (uint64_t)ceil(this->EF_LANES *
                this->scheduler->getMetadataBits() / (double)this->dram->getWidth()) * this->EF_ROWS;
        wgt_address_buffer = AddressBuffer(filter_sets * groups, AddressBufferSet(max_buffer_time,
                AddressBufferRow(accesses_per_filter, NULL_ADDR)));

        auto tiles = this->arch->getTiles();
        auto filter_sets_per_set = ceil(filter_sets / (double)tiles);
        wgt_address_map = std::vector<AddressRange>(filter_sets_per_set * groups, AddressRange());

        // Filter Set third
        for (int g = 0; g < groups; ++g) {

            for (int m = 0; m < filter_sets; m += tiles) {

                std::get<0>(wgt_address_map[g * filter_sets_per_set + m / tiles]) =
                        this->dram->getStartWgtAddress() + next_wgt_address;

                // Buffer depth second
                auto skip_buf = std::vector<int>(tiles, 0);
                for (int y = 0; y < max_buffer_time; ++y) {//Pariasim: 48

                    for (int t = 0; t < tiles; ++t) {

                        auto mm = g * filter_sets  + m + t;

                        if (mm >= filter_sets * (g + 1))
                            continue;

                        if (this->arch->schedule()) {
                            bool zero_line = this->scheduler->check_zero_line(this->weight_buffer[mm][y]);
                            if (skip_buf[t] < this->scheduler->getLookaheadH() && zero_line) {
                                skip_buf[t]++;
                                continue;
                            }
                            skip_buf[t] = 0;
                        }

                        // Buffer width first
                        for (int x = 0; x < accesses_per_filter; ++x) {
                            this->wgt_address_buffer[mm][y][x] = this->dram->getStartWgtAddress() + next_wgt_address;
                            next_wgt_address += this->dram->getWidth();
                        }

                    }
                }

                std::get<1>(wgt_address_map[g * filter_sets_per_set + m / tiles]) =
                        this->dram->getStartWgtAddress() + next_wgt_address - this->dram->getWidth();

            }
        }


        // Banks buffer
        wgt_bank_buffer = BankBuffer(filter_sets * groups, BankBufferSet(max_buffer_time,
                BankBufferRow(accesses_per_filter)));
        wgt_end_time = std::vector<uint64_t>(filter_sets * groups, 0);

        int bank = 0;
        for (int m = 0; m < filter_sets * groups; ++m) {
            for (int f = 0; f < accesses_per_filter; ++f) {

                int skip_buf = 0;
                for (int y = 0; y < max_buffer_time; ++y) {

                    if (this->arch->schedule()) {
                        bool zero_line = this->scheduler->check_zero_line(this->weight_buffer[m][y]);
                        if (skip_buf < this->scheduler->getLookaheadH() && zero_line) {
                            skip_buf++;
                            continue;
                        }
                        skip_buf = 0;
                    }

                    if (y > wgt_end_time[m])
                        wgt_end_time[m] = y;
                    this->wgt_bank_buffer[m][y][f] = bank;
                }

                bank = (bank + 1) % this->gbuffer->getWgtBanks();

            }
        }

    }

    template <typename T>
    void OutputStationary<T>::fill_window_buffer(uint32_t group_idx) {

        auto recurrence = std::static_pointer_cast<OutputStationary<T>::NodeOutS>
                (this->on_chip_graph.front())->recurrence;

        if (windows.empty()) {
            throw std::runtime_error("Window indices may not be empty");
        }

        auto num_windows = this->linear ? this->EF_COLUMNS : windows.size();
        window_buffer = BufferSet<T>(max_buffer_time, BufferRow<T>(num_windows * this->EF_LANES, {0.0f, 0, 0}));

        auto accesses_per_window = (uint64_t)ceil(this->EF_LANES / (double)this->dram->getActValuesPerBlock());
        window_address_buffer = AddressBufferSet(max_buffer_time, AddressBufferRow(accesses_per_window *
                windows.size(), NULL_ADDR));

        window_bank_buffer = BankBufferSet(max_buffer_time, BankBufferRow(accesses_per_window * windows.size(), -1));

        const std::vector<size_t> &act_shape = this->act->getShape();
        const std::vector<size_t> &wgt_shape = this->wgt->getShape();

        auto act_channels = this->_3dim ? act_shape[2] : act_shape[1];

        auto wgt_channels = wgt_shape[1];
        auto Kx = wgt_shape[2];
        auto Ky = wgt_shape[3];

        auto channels = depthwise ? filters_per_group : wgt_channels;

        int next_column = 0;
        for (int w = 0; w < windows.size(); ++w) {
            auto x_window = std::get<0>(windows[w]) * this->stride;
            auto y_window = std::get<1>(windows[w]) * this->stride;

            auto start_group = group_idx * channels;

            int buffer_time = 0;
            for (int y = 0; y < Ky; ++y) {
                for (int x = 0; x < Kx; ++x) {
                    for (int k = 0; k < channels; k += this->EF_LANES) {

                        int index = 0;
                        for (int ch = k; ch < std::min((uint64_t) k + this->EF_LANES, channels); ++ch) {

                            if ((start_group + ch) >= act_channels)
                                continue;

                            auto act_bits = this->_3dim ? this->act->get(0, recurrence, ch) :
                                    this->act->get(0, start_group + ch, x_window + x, y_window + y);

                            if (this->arch->diffy() && !this->linear) {
                                auto prev_act_bits = (x_window - this->stride < 0) ? 0 :
                                        this->act->get(0, start_group + ch, x_window + x - this->stride, y_window + y);
                                act_bits = (short)act_bits - (short)prev_act_bits;
                            }

                            auto column = this->linear ? next_column : w;
                            int pos = column * this->EF_LANES + index;
                            window_buffer[buffer_time][pos] = {act_bits, buffer_time, index};

                            int addr_pos = w * accesses_per_window + index / this->dram->getActValuesPerBlock();
                            window_address_buffer[buffer_time][addr_pos] = act_address_map[y_window + y]
                                    [x_window + x][(start_group + ch) / this->dram->getActValuesPerBlock()];

                            window_bank_buffer[buffer_time][addr_pos] = act_bank_map[y_window + y][x_window + x];

                            index++;
                            if (index == this->EF_LANES) {
                                buffer_time++;
                                index = 0;
                            }

                        }

                        if (index != 0) {
                            buffer_time++;
                        }

                        if (this->linear)
                            next_column = (next_column + 1) % this->EF_COLUMNS;

                    } // Activations channel
                } // Kernel X
            } // Kernel Y

        } // Windows

    }

    template<typename T>
    uint64_t OutputStationary<T>::calculate_outputs() {
        const auto &current_node = std::static_pointer_cast<NodeOutS>(this->on_chip_graph.front());

        const auto &time_step = current_node->time_step;
        const auto &max_time = current_node->max_time;

        auto total_outputs = 0;
        if ((time_step + 1) * max_time == this->max_buffer_time) {
            auto num_windows = out_x * out_y;
            auto start_window = current_node->window_sets.front() * this->EF_COLUMNS;
            auto total_windows = std::min(current_node->window_sets.size() * this->EF_COLUMNS,
                    (uint64_t) (num_windows - start_window));

            auto num_filters = this->wgt->getShape()[0];
            auto start_filter = current_node->filter_sets.front() * this->arch->getTiles() * this->EF_ROWS;
            auto total_filters = std::min(current_node->filter_sets.size() * this->arch->getTiles() * this->EF_ROWS,
                    num_filters - start_filter);
            total_outputs = total_windows * total_filters;
        }

        return total_outputs;
    }

    template <typename T>
    void OutputStationary<T>::configure_layer(const std::shared_ptr<base::Array<T>> &_act,
            const std::shared_ptr<base::Array<T>> &_wgt, uint32_t act_prec, uint32_t wgt_prec, bool _linear,
            bool _rnn, int _stride) {

        //Control<T>::configure_layer(this->EF_LANES, this->EF_COLUMNS, this->EF_ROWS, _act, _wgt, act_prec, wgt_prec, _linear, _rnn, _stride);
        Control<T>::configure_layer(_act, _wgt, act_prec, wgt_prec, _linear, _rnn, _stride);

        group_it = 0;
        window_set_it = 0;
        filter_set_it = 0;
        requested = 0;
        write = std::vector<bool>(this->arch->getTiles(), false);
        time = std::vector<int>(this->arch->getTiles(), 0);
        skip = std::vector<int>(this->arch->getTiles(), 0);
        window_buffer_filled = false;
        filter_buffer_filled = false;
        tiles_done = false;

        const std::vector<size_t> &act_shape = this->act->getShape();
        const std::vector<size_t> &wgt_shape = this->wgt->getShape();

        auto act_channels = this->_3dim ? act_shape[2] : act_shape[1];
        auto Nx = this->_3dim ? 1 : act_shape[2];
        auto Ny = this->_3dim ? 1 : act_shape[3];

        auto num_filters = wgt_shape[0];
        auto wgt_channels = wgt_shape[1];
        auto Kx = wgt_shape[2];
        auto Ky = wgt_shape[3];

        out_x = (Nx - Kx) / this->stride + 1;
        out_y = (Ny - Ky) / this->stride + 1;

        depthwise = wgt_channels == 1 && act_channels != 1;

        if (depthwise) {
            auto MIN_DIM = std::min(this->EF_LANES, this->EF_ROWS);
            this->EF_LANES = MIN_DIM;
            this->EF_ROWS = MIN_DIM;

            groups = (uint64_t)ceil(num_filters / (double)MIN_DIM);
            filters_per_group = MIN_DIM;
        } else {
            groups = act_channels / wgt_channels == 2 ? 2 : 1;
            filters_per_group = (uint64_t)ceil(num_filters / (double)groups);
        }

        window_sets = (uint64_t)ceil(out_x * out_y / (double)this->EF_COLUMNS);
        filter_sets = (uint64_t)ceil(filters_per_group / (double)this->EF_ROWS);

        // Generate weight buffer
        auto round_wgt_channels = (int)ceil(wgt_channels / (double)this->EF_LANES) * this->EF_LANES;
        max_buffer_time = (uint64_t)ceil(round_wgt_channels * Kx * Ky / (double)this->EF_LANES);

        fill_weight_buffer();
    }

    INITIALISE_DATA_TYPES(OutputStationary);

}
