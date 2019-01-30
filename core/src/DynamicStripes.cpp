
#include <core/DynamicStripes.h>

namespace core {

    /* AUXILIARY FUNCTIONS */

    template <typename T>
    uint16_t DynamicStripes<T>::computeDynamicStripesBitsPE(uint8_t layer_prec) {
        return layer_prec * (uint8_t)16;
    }

    template <typename T>
    uint8_t DynamicStripes<T>::computeDynamicStripesColumn(int batch, int act_x, int act_y, int kernel_x, int kernel_y,
            int init_channel, int stride, const cnpy::Array<T> &padded_act, int max_channel, const rowIdxMap &rowMap) {

        std::list<int> row_list;
        uint8_t fill_cycles = 0, max_bit = 0, min_bit = 16;
        for (int channel = init_channel; channel < std::min(init_channel + WEIGHT_LANES, max_channel); channel++) {

            // Fill cycles
            auto nmRow = rowMap[act_x + kernel_x][act_y + kernel_y][channel];
            auto it = std::find(row_list.begin(), row_list.end(), nmRow);
            if (it == row_list.end()) {
                row_list.push_back(nmRow);
                fill_cycles++;
            }

            // Computation cycles
            uint16_t act_bits = padded_act.get(batch, channel, stride * act_x + kernel_x, stride * act_y + kernel_y);

            uint8_t count = 0;
            std::vector<uint8_t> act_offsets;
            while (act_bits) {
                auto current_bit = act_bits & 1;
                if(current_bit) act_offsets.push_back(count);
                act_bits >>= 1;
                count++;
            }

            auto max_act_bit = act_offsets.empty() ? 0 : *std::max_element(act_offsets.begin(), act_offsets.end());
            auto min_act_bit = act_offsets.empty() ? 16 : *std::min_element(act_offsets.begin(), act_offsets.end());

            if(max_act_bit > max_bit) max_bit = max_act_bit;
            if(min_act_bit < min_bit) min_bit = min_act_bit;

        }

        uint8_t n_bits = min_bit > max_bit ? (uint8_t)1 : max_bit - min_bit + (uint8_t)1;
        return std::max(fill_cycles, n_bits);
    }

    template <typename T>
    uint8_t DynamicStripes<T>::computeDynamicStripesTile(int batch, const std::vector<int> &list_act_x,
            const std::vector<int> &list_act_y, int kernel_x, int kernel_y, int init_channel, int stride,
            const cnpy::Array<T> &padded_act, int max_channel, const rowIdxMap &rowMap) {

        std::list<int> row_list;
        std::vector<uint8_t> per_SIP_n_bits (list_act_x.size(), 0);
        uint8_t fill_cycles = 0, max_bit = 0, min_bit = 16;
        for(int window = 0; window < list_act_x.size(); window++) {
            if(PRECISION_GRANULARITY == "SIP") max_bit = 0, min_bit = 16;
            for (int channel = init_channel; channel < std::min(init_channel + WEIGHT_LANES, max_channel); channel++) {

                // Fill cycles
                auto nmRow = rowMap[list_act_x[window] + kernel_x][list_act_y[window] + kernel_y][channel];
                auto it = std::find(row_list.begin(), row_list.end(), nmRow);
                if (it == row_list.end()) {
                    row_list.push_back(nmRow);
                    fill_cycles++;
                }

                // Computation cycles
                uint16_t act_bits = padded_act.get(batch, channel, stride * list_act_x[window] + kernel_x,
                        stride * list_act_y[window] + kernel_y);

                uint8_t count = 0;
                std::vector<uint8_t> act_offsets;
                while (act_bits) {
                    auto current_bit = act_bits & 1;
                    if(current_bit) act_offsets.push_back(count);
                    act_bits >>= 1;
                    count++;
                }

                auto max_act_bit = act_offsets.empty() ? 0 : *std::max_element(act_offsets.begin(), act_offsets.end());
                auto min_act_bit = act_offsets.empty() ? 16 : *std::min_element(act_offsets.begin(), act_offsets.end());

                if(max_act_bit > max_bit) max_bit = max_act_bit;
                if(min_act_bit < min_bit) min_bit = min_act_bit;

            }
            per_SIP_n_bits[window] = min_bit > max_bit ? 1 : max_bit - min_bit + 1;
        }

        uint8_t n_bits = PRECISION_GRANULARITY == "SIP" ?
                *std::max_element(per_SIP_n_bits.begin(), per_SIP_n_bits.end()) :
                min_bit > max_bit ? 1 : max_bit - min_bit + 1;
        return std::max(fill_cycles, n_bits);
    }

    /* CYCLES */

    template <typename T>
    void DynamicStripes<T>::computeConvolution(const core::Layer<T> &layer, sys::Statistics::Stats &stats) {

        std::chrono::high_resolution_clock::time_point t1 = std::chrono::high_resolution_clock::now();

        cnpy::Array<T> act = layer.getActivations();
        act.powers_of_two_representation();
        cnpy::Array<T> wgt = layer.getWeights();
        if(wgt.getDimensions() == 2) wgt.reshape_to_4D();

        int padding = layer.getPadding();
        int stride = layer.getStride();

        act.zero_pad(padding);

        if(act.getShape()[1] == 3 && stride > 1) {
            act.reshape_first_layer_act((uint16_t)stride);
            wgt.reshape_first_layer_wgt((uint16_t)stride);
            stride = 1;
        }

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

        const auto &rowMap = this->generate_rowMap(Nx, Ny , act_channels, NM_WIDTH);
        long out_x = (Nx - Kx)/stride + 1;
        long out_y = (Ny - Ky)/stride + 1;

        int groups = act_channels / wgt_channels;
        auto num_filters_sets = (uint32_t)ceil(num_filters/(double)N_ROWS/groups);

        // Stats
        auto index = stats.cycles.size();
        stats.cycles.emplace_back(std::vector<uint32_t>(batch_size,0));

        std::vector<int> list_x, list_y;
        int n, x_counter, y_counter;

        // Convolution
        #ifdef OPENMP
        auto max_threads = omp_get_max_threads();
        omp_set_num_threads(std::min(max_threads,this->N_THREADS));
        #pragma omp parallel for private(n,x_counter,y_counter,list_x,list_y)
        #endif
        for(n=0; n<batch_size; n++) {
            x_counter = 0, y_counter = 0;
            while(this->iterateWindows(out_x,out_y,list_x,list_y,x_counter, y_counter, N_COLUMNS)) {
                for (int i = 0; i < Kx; i++) {
                    for (int j = 0; j < Ky; j++) {
                        for (int k = 0; k < act_channels; k += WEIGHT_LANES) {
                            stats.cycles[index][n] += computeDynamicStripesTile(n, list_x, list_y, i, j, k, stride, act,
                                    act_channels, rowMap);
                        }
                    }
                }
            }
            stats.cycles[index][n] *= num_filters_sets;
        }

        auto base_cycles = (uint32_t)(out_x * out_y * act_channels * Kx * Ky * num_filters_sets / N_ROWS);

        std::chrono::high_resolution_clock::time_point t2 = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(t2 - t1);

        stats.time.push_back(time_span);
        stats.baseline_cycles.push_back(base_cycles);

    }

    template <typename T>
    void DynamicStripes<T>::computeInnerProduct(const Layer<T> &layer, sys::Statistics::Stats &stats) {

        std::chrono::high_resolution_clock::time_point t1 = std::chrono::high_resolution_clock::now();

        cnpy::Array<T> act = layer.getActivations();
        act.powers_of_two_representation();
        if(act.getDimensions() == 4) act.reshape_to_2D();
        act.reshape_to_4D();

        const std::vector<size_t> &act_shape = act.getShape();
        const std::vector<size_t> &wgt_shape = layer.getWeights().getShape();

        int batch_size = act_shape[0];
        int act_channels = act_shape[1];
        int Nx = act_shape[2];
        int Ny = act_shape[3];
        if(this->FAST_MODE) batch_size = 1;

        int num_filters = wgt_shape[0];

        const auto &rowMap = this->generate_rowMap(Nx, Ny, act_channels, NM_WIDTH);

        auto num_filters_sets = (uint32_t)ceil(num_filters/(double)N_ROWS);

        // Stats
        auto index = stats.cycles.size();
        stats.cycles.emplace_back(std::vector<uint32_t>(batch_size,0));

        int n;

        #ifndef FC_MULTIPLEX_COLUMNS

        // All FC in one column
        #ifdef OPENMP
        auto max_threads = omp_get_max_threads();
        omp_set_num_threads(std::min(max_threads,this->N_THREADS));
        #pragma omp parallel for private(n)
        #endif
        for (n = 0; n<batch_size; n++) {
            for (int k = 0; k<act_channels; k += WEIGHT_LANES) {
                stats.cycles[index][n] += computeDynamicStripesColumn(n,0,0,0,0,k,0,act,act_channels,rowMap);
            }
            stats.cycles[index][n] *= num_filters_sets;
        }

        #else

        int column_index;
        std::vector<int> column_end;

        #ifdef OPENMP
        auto max_threads = omp_get_max_threads();
        omp_set_num_threads(std::min(max_threads,this->N_THREADS));
        #pragma omp parallel for private(n,column_index,column_end)
        #endif
        for (n = 0; n<batch_size; n++) {
            column_index = 0;
            column_end = std::vector<int>(this->N_COLUMNS, 0);
            for (int k = 0; k<act_channels; k += WEIGHT_LANES) {
                if(stats.cycles[index][n] < column_end[column_index]) stats.cycles[index][n] = column_end[column_index];
                auto column_cycles = computeDynamicStripesColumn(n,0,0,0,0,k,0,act,act_channels,rowMap);
                column_end[column_index] = stats.cycles[index][n] + column_cycles;
                stats.cycles[index][n]++;
                column_index++;
                if(column_index >= N_COLUMNS) column_index = 0;
            }
            stats.cycles[index][n] *= num_filters_sets;
        }

        #endif

        auto base_cycles = (uint32_t)(act_channels * num_filters_sets / N_ROWS);

        std::chrono::high_resolution_clock::time_point t2 = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(t2 - t1);

        stats.time.push_back(time_span);
        stats.baseline_cycles.push_back(base_cycles);

    }

    template <typename T>
    void DynamicStripes<T>::run(const Network<T> &network) {
        // Initialize statistics
        sys::Statistics::Stats stats;
        sys::Statistics::initialize(stats);

        stats.task_name = "cycles";
        stats.net_name = network.getName();
        stats.arch = "DynamicStripes_C" + std::to_string(N_COLUMNS) + "_R" + std::to_string(N_ROWS) + "_PG_" +
                PRECISION_GRANULARITY;

        for(const Layer<T> &layer : network.getLayers()) {
            if(layer.getType() == "Convolution") {
                stats.layers.push_back(layer.getName());
                stats.act_prec.push_back(std::get<0>(layer.getAct_precision()) + std::get<1>(layer.getAct_precision()));
                computeConvolution(layer, stats);
            } else if(layer.getType() == "InnerProduct") {
                stats.layers.push_back(layer.getName());
                stats.act_prec.push_back(std::get<0>(layer.getAct_precision()) + std::get<1>(layer.getAct_precision()));
                computeInnerProduct(layer, stats);
            }
        }

        // Set statistics to write
        sys::Statistics::addStats(stats);

    }

    /* POTENTIALS */

    template <typename T>
    void DynamicStripes<T>::computePotentialsConvolution(const core::Layer<T> &layer, sys::Statistics::Stats &stats) {

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

        // Get layer precision
        auto layer_prec = std::get<0>(layer.getAct_precision()) + std::get<1>(layer.getAct_precision());

        // Convolution
        for(n=0; n<batch_size; n++) {
            bit_counter = (uint64_t)computeDynamicStripesBitsPE((uint8_t)layer_prec) * out_x * out_y * Kx * Ky *
                    act_channels;
            work_reduction[n] = 100 - ((double)(bit_counter * num_filters_sets) / (double)parallel_mult / 256. * 100);
            speedup[n] = (double)parallel_mult * 256. / (double)(bit_counter * num_filters);
            bit_multiplications[n] = bit_counter * num_filters_sets;
        }

        std::chrono::high_resolution_clock::time_point t2 = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(t2 - t1);

        stats.time.push_back(time_span);
        stats.work_reduction.push_back(work_reduction);
        stats.speedup.push_back(speedup);
        stats.parallel_multiplications.push_back(parallel_mult);
        stats.bit_multiplications.push_back(bit_multiplications);

    }

    template <typename T>
    void DynamicStripes<T>::computePotentialsInnerProduct(const Layer<T> &layer, sys::Statistics::Stats &stats) {

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

        // Get layer precision
        auto layer_prec = std::get<0>(layer.getAct_precision()) + std::get<1>(layer.getAct_precision());

        for (n = 0; n<batch_size; n++) {
            bit_counter = computeDynamicStripesBitsPE((uint8_t)layer_prec)*(uint16_t)wgt_channels;
            work_reduction[n] = 100 - ((double)(bit_counter * num_filters) / (double) parallel_mult / 256. * 100);
            speedup[n] = (double)parallel_mult * 256. / (double)(bit_counter * num_filters);
            bit_multiplications[n] = bit_counter * num_filters;
        }

        std::chrono::high_resolution_clock::time_point t2 = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(t2 - t1);

        stats.time.push_back(time_span);
        stats.work_reduction.push_back(work_reduction);
        stats.speedup.push_back(speedup);
        stats.parallel_multiplications.push_back(parallel_mult);
        stats.bit_multiplications.push_back(bit_multiplications);

    }

    template <typename T>
    void DynamicStripes<T>::potentials(const Network<T> &network) {
        // Initialize statistics
        sys::Statistics::Stats stats;
        sys::Statistics::initialize(stats);

        stats.task_name = "potentials";
        stats.net_name = network.getName();
        stats.arch = "DynamicStripes";

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

    template class DynamicStripes<uint16_t>;

}
