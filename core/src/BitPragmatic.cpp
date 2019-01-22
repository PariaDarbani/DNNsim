
#include <core/BitPragmatic.h>

namespace core {

    /* AUXILIARY FUNCTIONS */

    template <typename T>
    uint8_t BitPragmatic<T>::computePragmaticBitsPE(uint16_t act) {

        uint16_t act_bits = act;

        #ifdef BOOTH_ENCODING
        act_bits = this->booth_encoding(act_bits);
        #endif

        uint8_t act_effectual_bits = 0;
        while (act_bits) {
            act_effectual_bits += act_bits & 1;
            act_bits >>= 1;
        }

        uint8_t bit_multiplications = act_effectual_bits * (uint8_t)16;
        #ifdef ZERO_COUNT
        if(bit_multiplications == 0) bit_multiplications = 1;
        #endif

        return bit_multiplications;
    }

    template <typename T>
    uint8_t BitPragmatic<T>::computePragmaticPE(const std::vector<std::queue<uint8_t>> &offsets) {

        // Two stages shifting
        uint8_t PE_cycles = 0;
        auto tmp_offsets = offsets;
        auto max_offset_first_stage = (uint8_t)((1 << BITS_FIRST_STAGE) - 1);

        bool still_ones = this->check_act_bits(tmp_offsets);
        while (still_ones) {

            // Get the offset for the second stage shift
            std::vector<uint8_t> last_bit;
            for (const auto &act_bits : tmp_offsets) {
                if(!act_bits.empty()) last_bit.push_back(act_bits.front());
            }
            // Must be one to enter the while loop
            uint8_t two_stage_offset = *std::min_element(last_bit.begin(), last_bit.end());
            auto max_offset = two_stage_offset + max_offset_first_stage;

            //Update values
            for (auto &act_bits : tmp_offsets) {
                if(!act_bits.empty() && act_bits.front() <= max_offset) act_bits.pop();
            }

            PE_cycles++;
            still_ones = this->check_act_bits(tmp_offsets);
        }

        #ifdef ZERO_COUNT
        if(PE_cycles == 0) PE_cycles = 1;
        #endif

        return PE_cycles;
    }

    template <typename T>
    uint8_t BitPragmatic<T>::computePragmaticColumn(int batch, int act_x, int act_y, int kernel_x, int kernel_y,
            int init_channel, int stride, const cnpy::Array<T> &padded_act, int max_channel) {

        std::vector<std::queue<uint8_t>> offsets;
        for(int channel = init_channel; channel < std::min(init_channel + WEIGHT_LANES,max_channel); channel++) {
            uint16_t act_bits = padded_act.get(batch, channel, stride * act_x + kernel_x, stride * act_y + kernel_y);
            #ifdef BOOTH_ENCODING
            act_bits = this->booth_encoding(act_bits);
            #endif

            uint8_t count = 0;
            std::queue<uint8_t> act_offsets;
            while (act_bits) {
                auto current_bit = act_bits & 1;
                if(current_bit) act_offsets.push(count);
                act_bits >>= 1;
                count++;
            }

            offsets.push_back(act_offsets);
        }

        return computePragmaticPE(offsets);

    }

    template <typename T>
    void BitPragmatic<T>::computePragmaticTile(int batch, const std::vector<int> &list_act_x,
            const std::vector<int> &list_act_y, int kernel_x, int kernel_y, int init_channel, int stride,
            const cnpy::Array<T> &padded_act, int max_channel, std::vector<uint32_t> &cycles_per_col,
            uint32_t &end_previous_pallet) {

        for(int window = 0; window < list_act_x.size(); window++) {
            uint8_t column_cycles = computePragmaticColumn(batch, list_act_x[window], list_act_y[window], kernel_x,
                    kernel_y, init_channel, stride, padded_act, max_channel);
            cycles_per_col[window] += column_cycles;
        }

        #ifdef TWO_REGISTERS_PER_SIP
        // Per-col synchronization assuming two registers per SIP
        for(auto &column_cycles : cycles_per_col) {
            if(column_cycles <= end_previous_pallet) {
                column_cycles = end_previous_pallet + 1;
            }
        }
        end_previous_pallet = *std::max_element(cycles_per_col.begin(), cycles_per_col.end());
        #else
        // Per-col synchronization assuming one register per SIP
        auto slowest_column = *std::max_element(cycles_per_col.begin(), cycles_per_col.end());
        cycles_per_col = std::vector<uint32_t>(N_COLUMNS,slowest_column);
        #endif
    }

    /* CYCLES */

    template <typename T>
    void BitPragmatic<T>::computeConvolution(const core::Layer<T> &layer, sys::Statistics::Stats &stats) {

        std::chrono::high_resolution_clock::time_point t1 = std::chrono::high_resolution_clock::now();

        cnpy::Array<T> act = layer.getActivations();
        act.powers_of_two_representation();
        cnpy::Array<T> wgt = layer.getWeights();
        if(wgt.getDimensions() == 2) wgt.reshape_to_4D();

        std::vector<size_t> act_shape = act.getShape();
        std::vector<size_t> wgt_shape = wgt.getShape();

        int batch_size = act_shape[0];
        int act_channels = act_shape[1];
        int Nx = act_shape[2];
        int Ny = act_shape[3];
        if(this->FAST_MODE) batch_size = 1;

        int num_filters = wgt_shape[0];
        int wgt_channels = wgt_shape[1];
        int Kx = wgt_shape[2];
        int Ky = wgt_shape[3];

        int padding = layer.getPadding();
        int stride = layer.getStride();

        cnpy::Array<T> padded_act = this->adjustPadding(act,padding);
        long out_x = (Nx - Kx + 2*padding)/stride + 1;
        long out_y = (Ny - Ky + 2*padding)/stride + 1;

        int groups = act_channels / wgt_channels;
        auto num_filters_sets = (uint32_t)ceil(num_filters/(double)N_ROWS/groups);

        // Stats
        std::vector<uint32_t> cycles (batch_size,0);
        std::vector<uint32_t> cycles_per_col;
        uint32_t end_previous_pallet;

        std::vector<int> list_x, list_y;
        int n, x_counter, y_counter;

        // Convolution
        #ifdef OPENMP
        auto max_threads = omp_get_max_threads();
        omp_set_num_threads(std::min(max_threads,this->N_THREADS));
        #pragma omp parallel for private(n,cycles_per_col,end_previous_pallet,x_counter,y_counter,list_x,list_y)
        #endif
        for(n=0; n<batch_size; n++) {
            end_previous_pallet = 0, x_counter = 0, y_counter = 0;
            cycles_per_col = std::vector<uint32_t>(N_COLUMNS, 0);
            while(this->iterateWindows(out_x,out_y,list_x,list_y,x_counter, y_counter, N_COLUMNS)) {
                for (int i = 0; i < Kx; i++) {
                    for (int j = 0; j < Ky; j++) {
                        for (int k = 0; k < act_channels; k += WEIGHT_LANES) {
                            computePragmaticTile(n,list_x, list_y, i, j, k, stride, padded_act, act_channels,
                                    cycles_per_col, end_previous_pallet);
                        }
                    }
                }
            }
            auto batch_cycles = *std::max_element(cycles_per_col.begin(), cycles_per_col.end());
            cycles[n] = batch_cycles*num_filters_sets;
        }

        auto base_cycles = (uint32_t)(out_x * out_y * act_channels * Kx * Ky * num_filters_sets / N_ROWS);
        auto avg_cycles = accumulate(cycles.begin(), cycles.end(), 0.0)/cycles.size();

        std::chrono::high_resolution_clock::time_point t2 = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(t2 - t1);

        stats.time.push_back(time_span);
        stats.cycles.push_back(cycles);
        stats.baseline_cycles.push_back(base_cycles);
        stats.avg_cycles.push_back((uint32_t)avg_cycles);
    }

    template <typename T>
    void BitPragmatic<T>::computeInnerProduct(const Layer<T> &layer, sys::Statistics::Stats &stats) {

        std::chrono::high_resolution_clock::time_point t1 = std::chrono::high_resolution_clock::now();

        cnpy::Array<T> act = layer.getActivations();
        act.powers_of_two_representation();
        if(act.getDimensions() == 4) act.reshape_to_2D();
        act.reshape_to_4D();

        const std::vector<size_t> &act_shape = act.getShape();
        const std::vector<size_t> &wgt_shape = layer.getWeights().getShape();

        int batch_size = act_shape[0];
        int act_channels = act_shape[1];
        int num_filters = wgt_shape[0];
        if(this->FAST_MODE) batch_size = 1;

        auto num_filters_sets = (uint32_t)ceil(num_filters/(double)N_ROWS);

        // Stats
        std::vector<uint32_t> cycles (batch_size,0);
        uint32_t batch_cycles;

        int n;

        #ifndef FC_MULTIPLEX_COLUMNS

        // All FC in one column
        #ifdef OPENMP
        auto max_threads = omp_get_max_threads();
        omp_set_num_threads(std::min(max_threads,this->N_THREADS));
        #pragma omp parallel for private(n,batch_cycles)
        #endif
        for (n = 0; n<batch_size; n++) {
            batch_cycles = 0;
            for (int k = 0; k<act_channels; k += WEIGHT_LANES) {
                batch_cycles += computePragmaticColumn(n,0,0,0,0,k,0,act,act_channels);
            }
            cycles[n] = batch_cycles*num_filters_sets;
        }

        #else

        int column_index;
        std::vector<int> column_end;

        #ifdef OPENMP
        auto max_threads = omp_get_max_threads();
        omp_set_num_threads(std::min(max_threads,this->N_THREADS));
        #pragma omp parallel for private(n,batch_cycles,column_index,column_end)
        #endif
        for (n = 0; n<batch_size; n++) {
            batch_cycles = 0, column_index = 0;
            column_end = std::vector<int>(this->N_COLUMNS, 0);
            for (int k = 0; k<act_channels; k += WEIGHT_LANES) {
                if(batch_cycles < column_end[column_index]) batch_cycles = column_end[column_index];
                auto column_cycles = computePragmaticColumn(n,0,0,0,0,k,0,act,act_channels);
                column_end[column_index] = batch_cycles + column_cycles;
                batch_cycles++;
                column_index++;
                if(column_index >= N_COLUMNS) column_index = 0;
            }
            cycles[n] = batch_cycles*num_filters_sets;
        }

        #endif

        auto base_cycles = (uint32_t)(act_channels * num_filters_sets / N_ROWS);
        auto avg_cycles = accumulate(cycles.begin(), cycles.end(), 0.0)/cycles.size();

        std::chrono::high_resolution_clock::time_point t2 = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(t2 - t1);

        stats.time.push_back(time_span);
        stats.cycles.push_back(cycles);
        stats.baseline_cycles.push_back(base_cycles);
        stats.avg_cycles.push_back((uint32_t)avg_cycles);

    }

    template <typename T>
    void BitPragmatic<T>::run(const Network<T> &network) {
        // Initialize statistics
        sys::Statistics::Stats stats;
        sys::Statistics::initialize(stats);

        stats.task_name = "cycles";
        stats.net_name = network.getName();
        stats.arch = "BitPragmatic_C" + std::to_string(N_COLUMNS) + "_R" + std::to_string(N_ROWS) + "_B" +
                std::to_string(BITS_FIRST_STAGE);

        for(const Layer<T> &layer : network.getLayers()) {
            if(layer.getType() == "Convolution") {
                stats.layers.push_back(layer.getName());
                computeConvolution(layer, stats);
            } else if (layer.getType() == "InnerProduct") {
                stats.layers.push_back(layer.getName());
                computeInnerProduct(layer,stats);
            }
        }

        // Set statistics to write
        sys::Statistics::addStats(stats);

    }

    /* POTENTIALS */

    template <typename T>
    void BitPragmatic<T>::computePotentialsConvolution(const core::Layer<T> &layer, sys::Statistics::Stats &stats) {

        std::chrono::high_resolution_clock::time_point t1 = std::chrono::high_resolution_clock::now();

        cnpy::Array<T> act = layer.getActivations();
        act.powers_of_two_representation();
        cnpy::Array<T> wgt = layer.getWeights();
        wgt.powers_of_two_representation();
        if(wgt.getDimensions() == 2) wgt.reshape_to_4D();

        const std::vector<size_t> &act_shape = act.getShape();
        const std::vector<size_t> &wgt_shape = wgt.getShape();

        int batch_size = act_shape[0];
        int act_channels = act_shape[1];
        int Nx = act_shape[2];
        int Ny = act_shape[3];
        if(this->FAST_MODE) batch_size = 1;

        int num_filters = wgt_shape[0];
        int wgt_channels = wgt_shape[1];
        int Kx = wgt_shape[2];
        int Ky = wgt_shape[3];

        int padding = layer.getPadding();
        int stride = layer.getStride();

        cnpy::Array<T> padded_act = this->adjustPadding(act,padding);
        long out_x = (Nx - Kx + 2*padding)/stride + 1;
        long out_y = (Ny - Ky + 2*padding)/stride + 1;

        int groups = act_channels / wgt_channels;
        auto num_filters_sets = (uint32_t)ceil((double)num_filters/groups);

        // Operations
        const auto parallel_mult = (uint64_t)(num_filters * out_x * out_y * Kx * Ky * wgt_channels);
        std::vector<uint64_t> bit_multiplications (batch_size,0);
        std::vector<double> work_reduction (batch_size,0);
        std::vector<double> speedup (batch_size,0);
        uint64_t bit_counter = 0;

        int n;

        // Convolution
        #ifdef OPENMP
        auto max_threads = omp_get_max_threads();
        omp_set_num_threads(std::min(max_threads,this->N_THREADS));
        #pragma omp parallel for private(n,bit_counter)
        #endif
        for(n=0; n<batch_size; n++) {
            bit_counter = 0;
            for(int x=0; x<out_x; x++) {
                for(int y=0; y<out_y; y++) {
                    for (int i = 0; i < Kx; i++) {
                        for (int j = 0; j < Ky; j++) {
                            for (int k = 0; k < act_channels; k ++) {
                                bit_counter += computePragmaticBitsPE(
                                        padded_act.get(n, k, stride * x + i,stride * y + j));
                            }
                        }
                    }
                }
            }
            work_reduction[n] = 100 - ((double)(bit_counter * num_filters_sets) / (double)parallel_mult / 256. * 100);
            speedup[n] = (double)parallel_mult * 256. / (double)(bit_counter * num_filters);
            bit_multiplications[n] = bit_counter * num_filters_sets;
        }

        auto avg_bit_multiplications = (uint64_t)accumulate(bit_multiplications.begin(), bit_multiplications.end(), 0.0)
                                       / bit_multiplications.size();
        auto avg_work_reduction = accumulate(work_reduction.begin(), work_reduction.end(), 0.0) / work_reduction.size();
        auto avg_speedup = accumulate(speedup.begin(), speedup.end(), 0.0) / speedup.size();

        std::chrono::high_resolution_clock::time_point t2 = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(t2 - t1);

        stats.time.push_back(time_span);
        stats.work_reduction.push_back(work_reduction);
        stats.avg_work_reduction.push_back(avg_work_reduction);
        stats.speedup.push_back(speedup);
        stats.avg_speedup.push_back(avg_speedup);
        stats.parallel_multiplications.push_back(parallel_mult);
        stats.bit_multiplications.push_back(bit_multiplications);
        stats.avg_bit_multiplications.push_back(avg_bit_multiplications);
    }

    template <typename T>
    void BitPragmatic<T>::computePotentialsInnerProduct(const Layer<T> &layer, sys::Statistics::Stats &stats) {

        std::chrono::high_resolution_clock::time_point t1 = std::chrono::high_resolution_clock::now();

        cnpy::Array<T> act = layer.getActivations();
        act.powers_of_two_representation();
        if(act.getDimensions() == 4) act.reshape_to_2D();
        cnpy::Array<T> wgt = layer.getWeights();
        wgt.powers_of_two_representation();

        const std::vector<size_t> &act_shape = act.getShape();
        const std::vector<size_t> &wgt_shape = wgt.getShape();

        int batch_size = act_shape[0];
        int num_filters = wgt_shape[0];
        int wgt_channels = wgt_shape[1];
        if(this->FAST_MODE) batch_size = 1;

        // Operations
        const auto parallel_mult = (uint64_t)num_filters * wgt_channels;
        std::vector<uint64_t> bit_multiplications (batch_size,0);
        std::vector<double> work_reduction (batch_size,0);
        std::vector<double> speedup (batch_size,0);
        uint64_t bit_counter = 0;

        int n;

        #ifdef OPENMP
        auto max_threads = omp_get_max_threads();
        omp_set_num_threads(std::min(max_threads,this->N_THREADS));
        #pragma omp parallel for private(n,bit_counter)
        #endif
        for (n = 0; n<batch_size; n++) {
            bit_counter = 0;
            for (int k = 0; k<wgt_channels; k++) {
                bit_counter += computePragmaticBitsPE(act.get(n, k));
            }
            work_reduction[n] = 100 - ((double)(bit_counter * num_filters) / (double) parallel_mult / 256. * 100);
            speedup[n] = (double)parallel_mult * 256. / (double)(bit_counter * num_filters);
            bit_multiplications[n] = bit_counter * num_filters;
        }

        auto avg_bit_multiplications = (uint64_t)accumulate(bit_multiplications.begin(), bit_multiplications.end(), 0.0)
                / bit_multiplications.size();
        auto avg_work_reduction = accumulate(work_reduction.begin(), work_reduction.end(), 0.0) / work_reduction.size();
        auto avg_speedup = accumulate(speedup.begin(), speedup.end(), 0.0) / speedup.size();

        std::chrono::high_resolution_clock::time_point t2 = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(t2 - t1);

        stats.time.push_back(time_span);
        stats.work_reduction.push_back(work_reduction);
        stats.avg_work_reduction.push_back(avg_work_reduction);
        stats.speedup.push_back(speedup);
        stats.avg_speedup.push_back(avg_speedup);
        stats.parallel_multiplications.push_back(parallel_mult);
        stats.bit_multiplications.push_back(bit_multiplications);
        stats.avg_bit_multiplications.push_back(avg_bit_multiplications);

    }

    template <typename T>
    void BitPragmatic<T>::potentials(const Network<T> &network) {
        // Initialize statistics
        sys::Statistics::Stats stats;
        sys::Statistics::initialize(stats);

        stats.task_name = "potentials";
        stats.net_name = network.getName();
        stats.arch = "BitPragmatic";

        for(const Layer<T> &layer : network.getLayers()) {
            if(layer.getType() == "Convolution") {
                stats.layers.push_back(layer.getName());
                stats.act_prec.push_back(std::get<0>(layer.getAct_precision()) + std::get<1>(layer.getAct_precision()));
                stats.wgt_prec.push_back(0);
                computePotentialsConvolution(layer,stats);
            } else if (layer.getType() == "InnerProduct") {
                stats.layers.push_back(layer.getName());
                stats.act_prec.push_back(std::get<0>(layer.getAct_precision()) + std::get<1>(layer.getAct_precision()));
                stats.wgt_prec.push_back(0);
                computePotentialsInnerProduct(layer,stats);
            }
        }

        // Set statistics to write
        sys::Statistics::addStats(stats);
    }

    /* MEMORY ACCESSES */

    template <typename T>
    void BitPragmatic<T>::computeMemAccessesConvolution(const core::Layer<T> &layer, sys::Statistics::Stats &stats) {
        const cnpy::Array<T> &wgt = layer.getWeights();
        const std::vector<size_t> &wgt_shape = wgt.getShape();
        const cnpy::Array<T> &act = layer.getActivations();
        const std::vector<size_t> &act_shape = act.getShape();

        int act_channels = act_shape[1];
        int Nx = act_shape[2];
        int Ny = act_shape[3];

        int num_filters = wgt_shape[0];
        int wgt_channels = wgt_shape[1];
        int Kx = wgt_shape[2];
        int Ky = wgt_shape[3];

        int padding = layer.getPadding();
        int stride = layer.getStride();

        long out_x = (Nx - Kx + 2*padding)/stride + 1;
        long out_y = (Ny - Ky + 2*padding)/stride + 1;

        //Memory stats - 16 bits
        int groups = act_channels / wgt_channels;
        auto num_filters_sets = (uint32_t)ceil(num_filters/(double)N_ROWS)/groups;
        auto num_activations_sets = (uint32_t)ceil(out_x*out_y/(double)N_COLUMNS);
        auto num_channel_sets = (uint32_t)ceil(wgt_channels/(double)WEIGHT_LANES);

        stats.on_chip_accesses_filters.push_back(num_filters_sets*num_activations_sets*num_channel_sets*Kx*Ky);
        stats.on_chip_accesses_activations.push_back(num_filters_sets*num_activations_sets*num_channel_sets*Kx*Ky);
        stats.off_chip_accesses_filters_sch3.push_back(1); // All filters per layer
        stats.off_chip_accesses_filters_sch4.push_back(num_filters_sets); // Working set of filters
        stats.off_chip_accesses_activations.push_back((uint32_t)out_y); // One row of activations
        stats.num_bytes_filters_sche3.push_back((uint32_t)(num_filters*wgt_channels*Kx*Ky*16)/8);
        stats.num_bytes_filters_sche4.push_back((uint32_t)(N_ROWS*wgt_channels*Kx*Ky*16)/8);
        stats.num_bytes_one_row_activations.push_back((uint32_t)(Nx*Ky*act_channels*16)/8);
        stats.num_computations.push_back(num_filters_sets*num_activations_sets*num_channel_sets*Kx*Ky);
    }


    template <typename T>
    void BitPragmatic<T>::memoryAccesses(const Network<T> &network) {
        // Initialize statistics
        sys::Statistics::Stats stats;
        sys::Statistics::initialize(stats);

        stats.task_name = "mem_accesses";
        stats.net_name = network.getName();
        stats.arch = "BitPragmatic_C" + std::to_string(N_COLUMNS) + "_R" + std::to_string(N_ROWS);

        for(const Layer<T> &layer : network.getLayers()) {
            if(layer.getType() == "Convolution") {
                stats.layers.push_back(layer.getName());
                computeMemAccessesConvolution(layer,stats);
            }
        }

        // Set statistics to write
        sys::Statistics::addStats(stats);
    }

    template class BitPragmatic<uint16_t>;

}