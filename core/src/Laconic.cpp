
#include <core/Laconic.h>

namespace core {

    template <typename T>
    void Laconic<T>::computeConvolution(const core::Layer<T> &layer, sys::Statistics::Stats &stats) {

    }

    template <typename T>
    void Laconic<T>::run(const Network<T> &network) {

    }

    uint8_t calculateEffectualBits(uint16_t act, uint16_t wgt, const std::tuple<int, int> &act_prec,
            const std::tuple<int, int> &wgt_prec) {
        int mag_act = std::get<0>(act_prec), prec_act = std::get<1>(act_prec);
        int mag_wgt = std::get<0>(wgt_prec), prec_wgt = std::get<1>(wgt_prec);
        uint16_t act_max = (1 << (mag_act + prec_act - 1)) - 1, wgt_max = (1 << (mag_wgt + prec_wgt - 1)) - 1;
        uint16_t act_bits = act & act_max, wgt_bits = wgt & wgt_max;

        uint8_t act_effectual_bits = 0;
        while (act_bits) {
            act_effectual_bits += act_bits & 1;
            act_bits >>= 1;
        }
        uint8_t wgt_effectual_bits = 0;
        while (wgt_bits) {
            wgt_effectual_bits += wgt_bits & 1;
            wgt_bits >>= 1;
        }
        
        return act_effectual_bits * wgt_effectual_bits;
    }

    template <typename T>
    void Laconic<T>::computeWorkReductionConvolution(const core::Layer<T> &layer, sys::Statistics::Stats &stats) {
        // Simplify names getting their pointers
        const cnpy::Array<T> &wgt = layer.getWeights();
        const std::vector<size_t> &wgt_shape = wgt.getShape();
        const cnpy::Array<T> &act = layer.getActivations();
        const std::vector<size_t> &act_shape = act.getShape();

        int padding = layer.getPadding();
        int stride = layer.getStride();
        int Kx = layer.getKx();
        int Ky = layer.getKy();

        cnpy::Array<T> padded_act = this->adjustPadding(act,padding);
        long out_x = (act_shape[2] - wgt_shape[2] + 2*padding)/stride + 1;
        long out_y = (act_shape[3] - wgt_shape[3] + 2*padding)/stride + 1;

        // Set filter batching
        int batches = (int)act.getShape()[1] / (int)wgt_shape[1];
        int it_per_batch = (int)wgt_shape[0] / batches;

        // Operations
        unsigned long mult_16bit = 0;
        unsigned long long effectual_bits = 0;

        // Convolution
        for(int n=0; n<1; n++) { // Calculate work reduction for first batch
            int current_batch = 0, batch_m =0, start_batch = 0;
            for(int m=0; m<wgt_shape[0]; m++) {
                for(int x=0; x<out_x; x++) {
                    for(int y=0; y<out_y; y++) {
                        for (int i = 0; i < Kx; i++) {
                            for (int j = 0; j < Ky; j++) {
                                for (int k = start_batch; k < wgt_shape[1] + start_batch; k++) {
                                    effectual_bits += calculateEffectualBits(
                                            padded_act.get(n, k, stride * x + i, stride * y + j),
                                            wgt.get(m, k - start_batch, i, j),
                                            layer.getAct_precision(),layer.getWgt_precision());
                                    mult_16bit++;
                                }
                            }
                        }
                    }
                }
                batch_m++;
                if(batch_m >= it_per_batch) {
                    batch_m = 0;
                    current_batch++;
                    start_batch = (int)wgt_shape[1]*current_batch;
                }
            }
        }
        double work_reduction = 100 - ((double)effectual_bits / (double)mult_16bit / 256. * 100);
        stats.work_reduction.push_back(work_reduction);
        stats.multiplications.push_back(mult_16bit);
        stats.effectual_bits.push_back(effectual_bits);
    }

    template <typename T>
    void Laconic<T>::workReduction(const Network<T> &network) {
        // Initialize statistics
        sys::Statistics::Stats stats;
        sys::Statistics::initialize(stats);

        stats.net_name = network.getName();
        stats.arch = "Laconic";

        for(const Layer<T> &layer : network.getLayers()) {
            if(layer.getType() == "Convolution") {
                stats.layers.push_back(layer.getName());
                stats.act_prec.push_back(std::get<0>(layer.getAct_precision())+std::get<1>(layer.getAct_precision()));
                stats.wgt_prec.push_back(std::get<0>(layer.getWgt_precision())+std::get<1>(layer.getWgt_precision()));
                computeWorkReductionConvolution(layer,stats);
            }
        }

        // Set statistics to write
        sys::Statistics::addStats(stats);
    }

    template class Laconic<uint16_t>;

}