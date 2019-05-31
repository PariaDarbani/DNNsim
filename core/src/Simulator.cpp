
#include <core/Simulator.h>

namespace core {

    /* COMMON FUNCTIONS */

    template <typename T>
    bool Simulator<T>::iterateWindows(long out_x, long out_y, std::vector<int> &list_x, std::vector<int> &list_y,
            int &x_counter, int &y_counter, int max_windows) {
        list_x.clear();
        list_y.clear();
        int current_windows = 0;
        while(x_counter < out_x) {
            while(y_counter < out_y) {
                list_x.push_back(x_counter);
                list_y.push_back(y_counter);
                current_windows++;
                y_counter++;
                if(current_windows >= max_windows)
                    return true;
            }
            y_counter = 0;
            x_counter++;
        }
        if(current_windows > 0)
            return true;

        x_counter = 0;
        return false;
    }

    template <typename T>
    std::tuple<uint8_t,uint8_t,uint8_t> Simulator<T>::split_bfloat16(float number) {
        bfloat16 bf_number = { .f = number };
        auto sign = (uint8_t)bf_number.field.sign;
        auto exponent = (uint8_t)bf_number.field.exponent;
        auto mantissa = (uint8_t)bf_number.field.mantissa;
        return std::make_tuple(sign,exponent,mantissa);
    }

    /* Only encode the values when get less number of bits */
    uint16_t generateBoothEncoding(uint16_t n) {
        uint32_t padded_n = n << 2;
        std::string bitstream = std::bitset<16 + 2>(padded_n).to_string();
        uint16_t booth_encoding = 0;
        bool booth = false;
        for(int i = 0; i < 16; i++) {
            std::string w = bitstream.substr(0,3);
            booth_encoding <<= 1;
            if(w == "000" || w == "001") {
                assert(!booth);
            } else if(w == "010") {
                if (booth) booth_encoding |= 0x1;
            } else if(w == "011") {
                if (booth) booth_encoding |= 0x1;
            } else if(w == "100") {
                if (!booth) booth_encoding |= 0x1;
                else { booth_encoding |= 0x1; booth = false;}
            } else if(w == "101") {
                if (!booth) booth_encoding |= 0x1;
            } else if(w == "110") {
                if (!booth) booth_encoding |= 0x1;
            } else if(w == "111") {
                if (!booth) { booth_encoding |= 0x2;  booth = true; }
            }
            bitstream = bitstream.substr(1);
        }
        return booth_encoding;
    }

    std::vector<uint16_t> generateBoothTable(const int MAX_VALUES = 32768) {
        std::vector<uint16_t> booth_table ((unsigned)MAX_VALUES, 0);
        for(int n = 0; n < MAX_VALUES; n++)
            booth_table[n] = generateBoothEncoding((uint16_t)n);
        return booth_table;
    }

    template <typename T>
    uint16_t Simulator<T>::booth_encoding(uint16_t value) {
        const static std::vector<uint16_t> booth_table = generateBoothTable();
        return booth_table[value];
    }

    std::vector<uint8_t> generateEffectualBitsTable(const int MAX_VALUES = 65535) {
        std::vector<uint8_t> effectual_bits_table ((unsigned)MAX_VALUES, 0);
        for(int n = 0; n < MAX_VALUES; n++) {

            auto tmp_n = n;
            uint8_t effectual_bits = 0;
            while (tmp_n) {
                effectual_bits += tmp_n & 1;
                tmp_n >>= 1;
            }

            effectual_bits_table[n] = effectual_bits;
        }
        return effectual_bits_table;
    }

    template <typename T>
    uint8_t Simulator<T>::effectualBits(uint16_t value) {
        const static std::vector<uint8_t> effectual_bits_table = generateEffectualBitsTable();
        return effectual_bits_table[value];
    }

    std::vector<std::tuple<uint8_t,uint8_t>> generateMinMaxTable(const int MAX_VALUES = 32768) {
        std::vector<std::tuple<uint8_t,uint8_t>> min_max_table ((unsigned)MAX_VALUES, std::tuple<uint8_t,uint8_t>());
        min_max_table[0] = std::make_tuple(16,0);
        for(int n = 1; n < MAX_VALUES; n++) {

            auto tmp_n = n;
            uint8_t count = 0;
            std::vector<uint8_t> offsets;
            while (tmp_n) {
                auto current_bit = tmp_n & 1;
                if(current_bit) offsets.push_back(count);
                tmp_n >>= 1;
                count++;
            }

            auto min_act_bit = offsets[0];
            auto max_act_bit = offsets[offsets.size()-1];

            min_max_table[n] = std::make_tuple(min_act_bit,max_act_bit);
        }
        return min_max_table;
    }

    template <typename T>
    std::tuple<uint8_t,uint8_t> Simulator<T>::minMax(uint16_t value) {
        const static std::vector<std::tuple<uint8_t,uint8_t>> min_max_table = generateMinMaxTable();
        return min_max_table[value];
    }

    template <typename T>
    bool Simulator<T>::check_act_bits(const std::vector<std::queue<uint8_t>> &offsets) {
        for (const auto &act_bits : offsets) {
            if (!act_bits.empty()) return true;
        }
        return false;
    }

    template <typename T>
    uint16_t Simulator<T>::sign_magnitude(short two_comp, uint16_t mask) {
        bool neg = two_comp < 0;
        int max_value = mask - 1;
        auto sign_mag = (uint16_t)abs(two_comp);
        sign_mag = (uint16_t)(sign_mag > max_value ? max_value : sign_mag);
        sign_mag = neg ? sign_mag | mask : sign_mag;
        return sign_mag;
    }

    /* DATA CALCULATIONS */

    template <typename T>
    void Simulator<T>::sparsity(const Network<T> &network) {
        // Initialize statistics
        sys::Statistics::Stats stats;
        sys::Statistics::initialize(stats);

        stats.task_name = "sparsity";
        stats.net_name = network.getName();
        stats.arch = "None";

        for(const Layer<T> &layer : network.getLayers()) {
            stats.layers.push_back(layer.getName());

            uint64_t zero_act = 0;
            const auto &act = layer.getActivations();
            for(uint64_t i = 0; i < act.getMax_index(); i++) {
                const auto data = act.get(i);
                if(data == 0) zero_act++;
            }

            uint64_t zero_wgt = 0;
            const auto &wgt = layer.getWeights();
            for(uint64_t i = 0; i < wgt.getMax_index(); i++) {
                const auto data = wgt.get(i);
                if(data == 0) zero_wgt++;
            }

            stats.act_sparsity.push_back(zero_act / (double)act.getMax_index() * 100.);
            stats.zero_act.push_back(zero_act);
            stats.total_act.push_back(act.getMax_index());
            stats.wgt_sparsity.push_back(zero_wgt / (double)wgt.getMax_index() * 100.);
            stats.zero_wgt.push_back(zero_wgt);
            stats.total_wgt.push_back(wgt.getMax_index());

        }

        // Set statistics to write
        sys::Statistics::addStats(stats);
    }

    template <typename uint16_t>
    void Simulator<uint16_t>::bit_sparsity(const Network<uint16_t> &network) {
        // Initialize statistics
        sys::Statistics::Stats stats;
        sys::Statistics::initialize(stats);

        stats.task_name = "bit_sparsity";
        stats.net_name = network.getName();
        stats.arch = "None";

        for(const Layer<uint16_t> &layer : network.getLayers()) {
            stats.layers.push_back(layer.getName());

            uint64_t zero_act_bits = 0;
            const auto &act = layer.getActivations();
            for(uint64_t i = 0; i < act.getMax_index(); i++) {
                const auto bits = act.get(i);
                uint8_t ones = effectualBits(bits);
                zero_act_bits += (16 - ones);
            }

            uint64_t zero_wgt_bits = 0;
            const auto &wgt = layer.getWeights();
            for(uint64_t i = 0; i < wgt.getMax_index(); i++) {
                const auto bits = wgt.get(i);
                uint8_t ones = effectualBits(bits);
                zero_wgt_bits += (16 - ones);
            }

            stats.act_sparsity.push_back(zero_act_bits / (act.getMax_index() * 16.) * 100.);
            stats.zero_act.push_back(zero_act_bits);
            stats.total_act.push_back(act.getMax_index() * 16);
            stats.wgt_sparsity.push_back(zero_wgt_bits / (wgt.getMax_index() * 16.) * 100.);
            stats.zero_wgt.push_back(zero_wgt_bits);
            stats.total_wgt.push_back(wgt.getMax_index() * 16);

        }

        // Set statistics to write
        sys::Statistics::addStats(stats);
    }

    template <typename T>
	void Simulator<T>::training_sparsity(const Network<T> &network, sys::Statistics::Stats &stats, int epoch,
	        int epochs) {

		if(epoch == 0) {
		    stats.task_name = "sparsity";
		    stats.net_name = network.getName();
		    stats.arch = "None";
		}

        for(int layer_it = 0; layer_it < network.getLayers().size(); layer_it++) {

			const Layer<T> &layer = network.getLayers()[layer_it];

			if(epoch == 0) {
            	stats.layers.push_back(layer.getName());

				stats.fw_act_sparsity.emplace_back(std::vector<double>(epochs,0));
        		stats.fw_zero_act.emplace_back(std::vector<uint64_t>(epochs,0));
        		stats.fw_total_act.emplace_back(std::vector<uint64_t>(epochs,0));
				stats.fw_wgt_sparsity.emplace_back(std::vector<double>(epochs,0));
        		stats.fw_zero_wgt.emplace_back(std::vector<uint64_t>(epochs,0));
        		stats.fw_total_wgt.emplace_back(std::vector<uint64_t>(epochs,0));
                stats.fw_bias_sparsity.emplace_back(std::vector<double>(epochs,0));
                stats.fw_zero_bias.emplace_back(std::vector<uint64_t>(epochs,0));
                stats.fw_total_bias.emplace_back(std::vector<uint64_t>(epochs,0));

				stats.bw_in_grad_sparsity.emplace_back(std::vector<double>(epochs,0));
        		stats.bw_zero_in_grad.emplace_back(std::vector<uint64_t>(epochs,0));
        		stats.bw_total_in_grad.emplace_back(std::vector<uint64_t>(epochs,0));
				stats.bw_wgt_grad_sparsity.emplace_back(std::vector<double>(epochs,0));
        		stats.bw_zero_wgt_grad.emplace_back(std::vector<uint64_t>(epochs,0));
        		stats.bw_total_wgt_grad.emplace_back(std::vector<uint64_t>(epochs,0));
                stats.bw_bias_grad_sparsity.emplace_back(std::vector<double>(epochs,0));
                stats.bw_zero_bias_grad.emplace_back(std::vector<uint64_t>(epochs,0));
                stats.bw_total_bias_grad.emplace_back(std::vector<uint64_t>(epochs,0));
                stats.bw_out_grad_sparsity.emplace_back(std::vector<double>(epochs,0));
                stats.bw_zero_out_grad.emplace_back(std::vector<uint64_t>(epochs,0));
                stats.bw_total_out_grad.emplace_back(std::vector<uint64_t>(epochs,0));
			}

			// Forward
            if(network.getForward()) {
                uint64_t zero_act = 0;
                const auto &act = layer.getActivations();
                for(uint64_t i = 0; i < act.getMax_index(); i++) {
                    const auto data = act.get(i);
                    if(data == 0) zero_act++;
                }

                uint64_t zero_wgt = 0;
                const auto &wgt = layer.getWeights();
                for(uint64_t i = 0; i < wgt.getMax_index(); i++) {
                    const auto data = wgt.get(i);
                    if(data == 0) zero_wgt++;
                }

                stats.fw_act_sparsity[layer_it][epoch] = zero_act / (double)act.getMax_index() * 100.;
                stats.fw_zero_act[layer_it][epoch] = zero_act;
                stats.fw_total_act[layer_it][epoch] = act.getMax_index();
                stats.fw_wgt_sparsity[layer_it][epoch] = zero_wgt / (double)wgt.getMax_index() * 100.;
                stats.fw_zero_wgt[layer_it][epoch] = zero_wgt;
                stats.fw_total_wgt[layer_it][epoch] = wgt.getMax_index();
            }

			//Backward
			if(network.getBackward()) {
                uint64_t zero_act_grad = 0;
                const auto &act_grad = layer.getInputGradients();
                if(layer_it != 0) {
                    for (uint64_t i = 0; i < act_grad.getMax_index(); i++) {
                        const auto data = act_grad.get(i);
                        if (data == 0) zero_act_grad++;
                    }
                }

                uint64_t zero_wgt_grad = 0;
                const auto &wgt_grad = layer.getWeightGradients();
                for(uint64_t i = 0; i < wgt_grad.getMax_index(); i++) {
                    const auto data = wgt_grad.get(i);
                    if(data == 0) zero_wgt_grad++;
                }

                uint64_t zero_out_act_grad = 0;
                const auto &out_act_grad = layer.getOutputGradients();
                for (uint64_t i = 0; i < out_act_grad.getMax_index(); i++) {
                    const auto data = out_act_grad.get(i);
                    if (data == 0) zero_out_act_grad++;
                }

                if(layer_it != 0) {
                    stats.bw_in_grad_sparsity[layer_it][epoch] = zero_act_grad / (double)act_grad.getMax_index() * 100.;
                    stats.bw_zero_in_grad[layer_it][epoch] = zero_act_grad;
                    stats.bw_total_in_grad[layer_it][epoch] = act_grad.getMax_index();
                }
                stats.bw_wgt_grad_sparsity[layer_it][epoch] = zero_wgt_grad / (double)wgt_grad.getMax_index() * 100.;
                stats.bw_zero_wgt_grad[layer_it][epoch] = zero_wgt_grad;
                stats.bw_total_wgt_grad[layer_it][epoch] = wgt_grad.getMax_index();
                stats.bw_out_grad_sparsity[layer_it][epoch] = zero_out_act_grad /
                        (double)out_act_grad.getMax_index() * 100.;
                stats.bw_zero_out_grad[layer_it][epoch] = zero_out_act_grad;
                stats.bw_total_out_grad[layer_it][epoch] = out_act_grad.getMax_index();
			}

            if(!layer.getBias().getShape().empty()) {

                uint64_t zero_bias = 0;
                const auto &bias = layer.getBias();
                for(uint64_t i = 0; i < bias.getMax_index(); i++) {
                    const auto data = bias.get(i);
                    if(data == 0) zero_bias++;
                }

                stats.fw_bias_sparsity[layer_it][epoch] = zero_bias / (double)bias.getMax_index() * 100.;
                stats.fw_zero_bias[layer_it][epoch] = zero_bias;
                stats.fw_total_bias[layer_it][epoch] = bias.getMax_index();
            }

            if(!layer.getBiasGradients().getShape().empty()) {

                uint64_t zero_bias_grad = 0;
                const auto &bias_grad = layer.getBiasGradients();
                for(uint64_t i = 0; i < bias_grad.getMax_index(); i++) {
                    const auto data = bias_grad.get(i);
                    if(data == 0) zero_bias_grad++;
                }

                stats.bw_bias_grad_sparsity[layer_it][epoch] = zero_bias_grad / (double)bias_grad.getMax_index() * 100.;
                stats.bw_zero_bias_grad[layer_it][epoch] = zero_bias_grad;
                stats.bw_total_bias_grad[layer_it][epoch] = bias_grad.getMax_index();
            }

        }

	}

    template <typename T>
    void Simulator<T>::training_bit_sparsity(const Network<T> &network, sys::Statistics::Stats &stats, int epoch,
            int epochs, bool mantissa) {

        if(epoch == 0) {
            stats.task_name = mantissa ? "mantissa_bit_sparsity" : "exponent_bit_sparsity";
            stats.net_name = network.getName();
            stats.arch = "None";
        }

        const auto MAX_ONES = mantissa ? 7. : 8.;

        for(int layer_it = 0; layer_it < network.getLayers().size(); layer_it++) {

            const Layer<T> &layer = network.getLayers()[layer_it];

            if(epoch == 0) {
                stats.layers.push_back(layer.getName());

                stats.fw_act_sparsity.emplace_back(std::vector<double>(epochs,0));
                stats.fw_zero_act.emplace_back(std::vector<uint64_t>(epochs,0));
                stats.fw_total_act.emplace_back(std::vector<uint64_t>(epochs,0));
                stats.fw_wgt_sparsity.emplace_back(std::vector<double>(epochs,0));
                stats.fw_zero_wgt.emplace_back(std::vector<uint64_t>(epochs,0));
                stats.fw_total_wgt.emplace_back(std::vector<uint64_t>(epochs,0));
                stats.fw_bias_sparsity.emplace_back(std::vector<double>(epochs,0));
                stats.fw_zero_bias.emplace_back(std::vector<uint64_t>(epochs,0));
                stats.fw_total_bias.emplace_back(std::vector<uint64_t>(epochs,0));

                stats.bw_in_grad_sparsity.emplace_back(std::vector<double>(epochs,0));
                stats.bw_zero_in_grad.emplace_back(std::vector<uint64_t>(epochs,0));
                stats.bw_total_in_grad.emplace_back(std::vector<uint64_t>(epochs,0));
                stats.bw_wgt_grad_sparsity.emplace_back(std::vector<double>(epochs,0));
                stats.bw_zero_wgt_grad.emplace_back(std::vector<uint64_t>(epochs,0));
                stats.bw_total_wgt_grad.emplace_back(std::vector<uint64_t>(epochs,0));
                stats.bw_bias_grad_sparsity.emplace_back(std::vector<double>(epochs,0));
                stats.bw_zero_bias_grad.emplace_back(std::vector<uint64_t>(epochs,0));
                stats.bw_total_bias_grad.emplace_back(std::vector<uint64_t>(epochs,0));
                stats.bw_out_grad_sparsity.emplace_back(std::vector<double>(epochs,0));
                stats.bw_zero_out_grad.emplace_back(std::vector<uint64_t>(epochs,0));
                stats.bw_total_out_grad.emplace_back(std::vector<uint64_t>(epochs,0));
            }

            // Forward
            if(network.getForward()) {
                uint64_t zero_act_bits = 0;
                const auto &act = layer.getActivations();
                for(uint64_t i = 0; i < act.getMax_index(); i++) {
                    const auto data_float = act.get(i);
                    auto data_bfloat = this->split_bfloat16(data_float);
                    auto bin_value = mantissa ? std::get<2>(data_bfloat) : std::get<1>(data_bfloat);
                    auto ones = effectualBits(bin_value);
                    zero_act_bits += (MAX_ONES - ones);
                }

                uint64_t zero_wgt_bits = 0;
                const auto &wgt = layer.getWeights();
                for(uint64_t i = 0; i < wgt.getMax_index(); i++) {
                    const auto data_float = wgt.get(i);
                    auto data_bfloat = this->split_bfloat16(data_float);
                    auto bin_value = mantissa ? std::get<2>(data_bfloat) : std::get<1>(data_bfloat);
                    auto ones = effectualBits(bin_value);
                    zero_wgt_bits += (MAX_ONES - ones);
                }

                stats.fw_act_sparsity[layer_it][epoch] = zero_act_bits / (act.getMax_index() * MAX_ONES) * 100.;
                stats.fw_zero_act[layer_it][epoch] = zero_act_bits;
                stats.fw_total_act[layer_it][epoch] = act.getMax_index() * MAX_ONES;
                stats.fw_wgt_sparsity[layer_it][epoch] = zero_wgt_bits / (wgt.getMax_index() * MAX_ONES) * 100.;
                stats.fw_zero_wgt[layer_it][epoch] = zero_wgt_bits;
                stats.fw_total_wgt[layer_it][epoch] = wgt.getMax_index() * MAX_ONES;
            }

            //Backward
            if(network.getBackward()) {
                uint64_t zero_act_grad_bits = 0;
                const auto &act_grad = layer.getInputGradients();
                if(layer_it != 0) {
                    for (uint64_t i = 0; i < act_grad.getMax_index(); i++) {
                        const auto data_float = act_grad.get(i);
                        auto data_bfloat = this->split_bfloat16(data_float);
                        auto bin_value = mantissa ? std::get<2>(data_bfloat) : std::get<1>(data_bfloat);
                        auto ones = effectualBits(bin_value);
                        zero_act_grad_bits += (MAX_ONES - ones);
                    }
                }

                uint64_t zero_wgt_grad_bits = 0;
                const auto &wgt_grad = layer.getWeightGradients();
                for(uint64_t i = 0; i < wgt_grad.getMax_index(); i++) {
                    const auto data_float = wgt_grad.get(i);
                    auto data_bfloat = this->split_bfloat16(data_float);
                    auto bin_value = mantissa ? std::get<2>(data_bfloat) : std::get<1>(data_bfloat);
                    auto ones = effectualBits(bin_value);
                    zero_wgt_grad_bits += (MAX_ONES - ones);
                }

                uint64_t zero_out_act_grad_bits = 0;
                const auto &out_act_grad = layer.getOutputGradients();
                for (uint64_t i = 0; i < out_act_grad.getMax_index(); i++) {
                    const auto data_float = out_act_grad.get(i);
                    auto data_bfloat = this->split_bfloat16(data_float);
                    auto bin_value = mantissa ? std::get<2>(data_bfloat) : std::get<1>(data_bfloat);
                    auto ones = effectualBits(bin_value);
                    zero_out_act_grad_bits += (MAX_ONES - ones);
                }

                if(layer_it != 0) {
                    stats.bw_in_grad_sparsity[layer_it][epoch] = zero_act_grad_bits /
                            (act_grad.getMax_index() * MAX_ONES) * 100.;
                    stats.bw_zero_in_grad[layer_it][epoch] = zero_act_grad_bits;
                    stats.bw_total_in_grad[layer_it][epoch] = act_grad.getMax_index() * MAX_ONES;
                }
                stats.bw_wgt_grad_sparsity[layer_it][epoch] = zero_wgt_grad_bits /
                        (wgt_grad.getMax_index() * MAX_ONES) * 100.;
                stats.bw_zero_wgt_grad[layer_it][epoch] = zero_wgt_grad_bits;
                stats.bw_total_wgt_grad[layer_it][epoch] = wgt_grad.getMax_index() * MAX_ONES;
                stats.bw_out_grad_sparsity[layer_it][epoch] = zero_out_act_grad_bits /
                        (out_act_grad.getMax_index() * MAX_ONES) * 100.;
                stats.bw_zero_out_grad[layer_it][epoch] = zero_out_act_grad_bits;
                stats.bw_total_out_grad[layer_it][epoch] = out_act_grad.getMax_index() * MAX_ONES;
            }

            if(!layer.getBias().getShape().empty()) {

                uint64_t zero_bias_bits = 0;
                const auto &bias = layer.getBias();
                for(uint64_t i = 0; i < bias.getMax_index(); i++) {
                    const auto data_float = bias.get(i);
                    auto data_bfloat = this->split_bfloat16(data_float);
                    auto bin_value = mantissa ? std::get<2>(data_bfloat) : std::get<1>(data_bfloat);
                    auto ones = effectualBits(bin_value);
                    zero_bias_bits += (MAX_ONES - ones);
                }

                stats.fw_bias_sparsity[layer_it][epoch] = zero_bias_bits / (bias.getMax_index() * MAX_ONES) * 100.;
                stats.fw_zero_bias[layer_it][epoch] = zero_bias_bits;
                stats.fw_total_bias[layer_it][epoch] = bias.getMax_index() * MAX_ONES;
            }

            if(!layer.getBiasGradients().getShape().empty()) {

                uint64_t zero_bias_grad_bits = 0;
                const auto &bias_grad = layer.getBiasGradients();
                for(uint64_t i = 0; i < bias_grad.getMax_index(); i++) {
                    const auto data_float = bias_grad.get(i);
                    auto data_bfloat = this->split_bfloat16(data_float);
                    auto bin_value = mantissa ? std::get<2>(data_bfloat) : std::get<1>(data_bfloat);
                    auto ones = effectualBits(bin_value);
                    zero_bias_grad_bits += (MAX_ONES - ones);
                }

                stats.bw_bias_grad_sparsity[layer_it][epoch] = zero_bias_grad_bits /
                        (bias_grad.getMax_index() * MAX_ONES) * 100.;
                stats.bw_zero_bias_grad[layer_it][epoch] = zero_bias_grad_bits;
                stats.bw_total_bias_grad[layer_it][epoch] = bias_grad.getMax_index() * MAX_ONES;
            }

        }

    }


    template <typename T>
    void Simulator<T>::training_distribution(const Network<T> &network, sys::Statistics::Stats &stats,
            int epoch, int epochs, bool mantissa) {

        const auto MAX_VALUE = mantissa ? 128 : 256;

        if(epoch == 0) {
            stats.task_name = mantissa ? "mantissa_distribution" : "exponent_distribution";
            stats.net_name = network.getName();
            stats.arch = "None";
            stats.mantissa_data = mantissa;

            stats.fw_act_values = std::vector<std::vector<std::vector<uint64_t>>>(MAX_VALUE);
            stats.fw_wgt_values = std::vector<std::vector<std::vector<uint64_t>>>(MAX_VALUE);
            stats.fw_bias_values = std::vector<std::vector<std::vector<uint64_t>>>(MAX_VALUE);
            stats.bw_in_grad_values = std::vector<std::vector<std::vector<uint64_t>>>(MAX_VALUE);
            stats.bw_wgt_grad_values = std::vector<std::vector<std::vector<uint64_t>>>(MAX_VALUE);
            stats.bw_bias_grad_values = std::vector<std::vector<std::vector<uint64_t>>>(MAX_VALUE);
            stats.bw_out_grad_values = std::vector<std::vector<std::vector<uint64_t>>>(MAX_VALUE);
        }

        for(int layer_it = 0; layer_it < network.getLayers().size(); layer_it++) {

            const Layer<T> &layer = network.getLayers()[layer_it];

            if(epoch == 0) {
                stats.layers.push_back(layer.getName());

                for(int i = 0; i < MAX_VALUE; i++) {
                    stats.fw_act_values[i].emplace_back(std::vector<uint64_t>(epochs, 0));
                    stats.fw_wgt_values[i].emplace_back(std::vector<uint64_t>(epochs, 0));
                    stats.fw_bias_values[i].emplace_back(std::vector<uint64_t>(epochs, 0));
                    stats.bw_in_grad_values[i].emplace_back(std::vector<uint64_t>(epochs, 0));
                    stats.bw_wgt_grad_values[i].emplace_back(std::vector<uint64_t>(epochs, 0));
                    stats.bw_bias_grad_values[i].emplace_back(std::vector<uint64_t>(epochs, 0));
                    stats.bw_out_grad_values[i].emplace_back(std::vector<uint64_t>(epochs, 0));
                }
            }

            // Forward
            if(network.getForward()) {
                const auto &act = layer.getActivations();
                for(uint64_t i = 0; i < act.getMax_index(); i++) {
                    const auto data_float = act.get(i);
                    auto data_bfloat = this->split_bfloat16(data_float);
                    auto bin_value = mantissa ? std::get<2>(data_bfloat) : std::get<1>(data_bfloat);
                    stats.fw_act_values[bin_value][layer_it][epoch]++;
                }

                const auto &wgt = layer.getWeights();
                for(uint64_t i = 0; i < wgt.getMax_index(); i++) {
                    const auto data_float = wgt.get(i);
                    auto data_bfloat = this->split_bfloat16(data_float);
                    auto bin_value = mantissa ? std::get<2>(data_bfloat) : std::get<1>(data_bfloat);
                    stats.fw_wgt_values[bin_value][layer_it][epoch]++;
                }
            }

            //Backward
            if(network.getBackward()) {
                const auto &act_grad = layer.getInputGradients();
                if(layer_it != 0) {
                    for (uint64_t i = 0; i < act_grad.getMax_index(); i++) {
                        const auto data_float = act_grad.get(i);
                        auto data_bfloat = this->split_bfloat16(data_float);
                        auto bin_value = mantissa ? std::get<2>(data_bfloat) : std::get<1>(data_bfloat);
                        stats.bw_in_grad_values[bin_value][layer_it][epoch]++;
                    }
                }

                const auto &wgt_grad = layer.getWeightGradients();
                for(uint64_t i = 0; i < wgt_grad.getMax_index(); i++) {
                    const auto data_float = wgt_grad.get(i);
                    auto data_bfloat = this->split_bfloat16(data_float);
                    auto bin_value = mantissa ? std::get<2>(data_bfloat) : std::get<1>(data_bfloat);
                    stats.bw_wgt_grad_values[bin_value][layer_it][epoch]++;
                }

                const auto &out_act_grad = layer.getOutputGradients();
                for (uint64_t i = 0; i < out_act_grad.getMax_index(); i++) {
                    const auto data_float = out_act_grad.get(i);
                    auto data_bfloat = this->split_bfloat16(data_float);
                    auto bin_value = mantissa ? std::get<2>(data_bfloat) : std::get<1>(data_bfloat);
                    stats.bw_out_grad_values[bin_value][layer_it][epoch]++;
                }
            }

            if(!layer.getBias().getShape().empty()) {

                const auto &bias = layer.getBias();
                for(uint64_t i = 0; i < bias.getMax_index(); i++) {
                    const auto data_float = bias.get(i);
                    auto data_bfloat = this->split_bfloat16(data_float);
                    auto bin_value = mantissa ? std::get<2>(data_bfloat) : std::get<1>(data_bfloat);
                    stats.fw_bias_values[bin_value][layer_it][epoch]++;
                }
            }

            if(!layer.getBiasGradients().getShape().empty()) {

                const auto &bias_grad = layer.getBiasGradients();
                for(uint64_t i = 0; i < bias_grad.getMax_index(); i++) {
                    const auto data_float = bias_grad.get(i);
                    auto data_bfloat = this->split_bfloat16(data_float);
                    auto bin_value = mantissa ? std::get<2>(data_bfloat) : std::get<1>(data_bfloat);
                    stats.bw_bias_grad_values[bin_value][layer_it][epoch]++;
                }
            }

        }

    }

    INITIALISE_DATA_TYPES(Simulator);

}
