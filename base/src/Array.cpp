
#include <base/Array.h>

namespace base {

    /* SETTERS */

    template <typename T>
    Array<T>::Array(const Array2D &_data, const std::vector<size_t> &_shape) {
        this->data = Array4D(_shape[0], Array3D(_shape[1], Array2D(1, Array1D(1))));
        for(int i = 0; i < this->shape[0]; i++) {
            for(int j = 0; j < this->shape[1]; j++)
                this->data[i][j][0][0] = _data[i][j];
        }
        this->shape = _shape;
        this->shape.push_back(1);
        this->shape.push_back(1);
    }

    template <typename T>
    Array<T>::Array(const Array4D &_data, const std::vector<size_t> &_shape) {
        this->data = Array4D(_shape[0], Array3D(_shape[1], Array2D(_shape[2], Array1D(_shape[3]))));
        auto coef1 = shape[1]*shape[2]*shape[3];
        auto coef2 = shape[2]*shape[3];
        for(int i = 0; i < this->shape[0]; i++) {
            for(int j = 0; j < this->shape[1]; j++) {
                for(int k = 0; k < this->shape[2]; k++) {
                    for(int l = 0; l < this->shape[3]; l++)
                        this->data[i][j][k][l] = _data[i][j][k][l];
                }
            }
        }
        this->shape = _shape;
    }

    template <typename T>
    void Array<T>::set_values(const std::string &path) {
        base::NpyArray data_npy;
        base::npy_load(path, data_npy, shape);
        std::vector<T> flat_array = data_npy.as_vec<T>();

        if (shape.size() == 1) {
            shape.push_back(1);
            shape.push_back(1);
            shape.push_back(1);
        } else if (shape.size() == 2) {
            shape.push_back(1);
            shape.push_back(1);
        } else if (shape.size() == 3) {
            shape.push_back(1);
        }

        uint64_t last3Dim = shape[1] * shape[2] * shape[3];
        uint64_t last2Dim = shape[2] * shape[3];
        data = Array4D(shape[0], Array3D(shape[1], Array2D(shape[2], Array1D(shape[3]))));

        for (uint64_t i = 0; i < shape[0]; i++) {
            for (uint64_t j = 0; j < shape[1]; j++) {
                for (uint64_t k = 0; k < shape[2]; k++) {
                    for (uint64_t l = 0; l < shape[3]; l++) {
                        auto value = flat_array[last3Dim * i + last2Dim * j + shape[3] * k + l];
                        data[i][j][k][l] = value;
                    }
                }
            }
        }
    }

    template <typename T>
    void Array<T>::set_values(const std::vector<T> &_data, const std::vector<size_t> &_shape) {
        if (_shape.size() == 1) {
            this->data = Array4D(_shape[0], Array3D(1, Array2D(1, Array1D(1))));
            for(int i = 0; i < _shape[0]; i++) {
                this->data[i][0][0][0] = _data[i];
            }
            this->shape = _shape;
            this->shape.push_back(1);
            this->shape.push_back(1);
            this->shape.push_back(1);
        } else if(_shape.size() == 2){
            this->data = Array4D(_shape[0], Array3D(_shape[1], Array2D(1, Array1D(1))));
            for(int i = 0; i < _shape[0]; i++) {
                for(int j = 0; j < _shape[1]; j++)
                    this->data[i][j][0][0] = _data[_shape[1]*i + j];
            }
            this->shape = _shape;
            this->shape.push_back(1);
            this->shape.push_back(1);
        } else if (_shape.size() == 3) {
            this->data = Array4D(_shape[0], Array3D(_shape[1], Array2D(_shape[2], Array1D(1))));
            unsigned long coef1 = _shape[1]*_shape[2];
            for(int i = 0; i < _shape[0]; i++) {
                for(int j = 0; j < _shape[1]; j++) {
                    for(int k = 0; k < _shape[2]; k++)
                        this->data[i][j][k][0] = _data[coef1*i + _shape[2]*j + k];
                }
            }
            this->shape = _shape;
            this->shape.push_back(1);
        } else if (_shape.size() == 4) {
            this->data = Array4D(_shape[0], Array3D(_shape[1], Array2D(_shape[2], Array1D(_shape[3]))));
            auto coef1 = _shape[1]*_shape[2]*_shape[3];
            auto coef2 = _shape[2]*_shape[3];
            for(int i = 0; i < _shape[0]; i++) {
                for(int j = 0; j < _shape[1]; j++) {
                    for(int k = 0; k < _shape[2]; k++) {
                        for(int l = 0; l < _shape[3]; l++)
                            this->data[i][j][k][l] = _data[coef1*i + coef2*j + _shape[3]*k + l];
                    }
                }
            }
            this->shape = _shape;
        } else throw std::runtime_error("Array dimensions error");
    }

    /* GETTERS */

    template <typename T>
    T Array<T>::get (int i, int j, int k, int l) const {
        #ifdef DEBUG
        if(getDimensions() != 4)
            throw std::runtime_error("4D Array dimensions error");
        #endif
        return this->data[i][j][k][l];
    }

    template <typename T>
    T Array<T>::get (int i, int j, int k) const {
        #ifdef DEBUG
        if(getDimensions() != 3)
            throw std::runtime_error("3D Array dimensions error");
        #endif
        return this->data[i][j][k][0];
    }

    template <typename T>
    T Array<T>::get (int i, int j) const {
        #ifdef DEBUG
        if(getDimensions() != 2)
            throw std::runtime_error("2D Array dimensions error");
        #endif
        return this->data[i][j][0][0];
    }

    template <typename T>
    T max_1D(const std::vector<T> &vector) {
        return *std::max_element(vector.begin(), vector.end());
    }

    template <typename T>
    T max_2D(const std::vector<std::vector<T>> &vector) {
        std::vector<T> maximums = std::vector<T>(vector.size(),0);
        for(int i = 0; i < vector.size(); i++) {
            maximums[i] = max_1D(vector[i]);
        }
        return max_1D(maximums);
    }

    template <typename T>
    T max_3D(const std::vector<std::vector<std::vector<T>>> &vector) {
        std::vector<T> maximums = std::vector<T>(vector.size(),0);
        for(int i = 0; i < vector.size(); i++) {
            maximums[i] = max_2D(vector[i]);
        }
        return max_1D(maximums);
    }

    template <typename T>
    T max_4D(const std::vector<std::vector<std::vector<std::vector<T>>>> &vector) {
        std::vector<T> maximums = std::vector<T>(vector.size(),0);
        for(int i = 0; i < vector.size(); i++) {
            maximums[i] = max_3D(vector[i]);
        }
        return max_1D(maximums);
    }

    template <typename T>
    T min_1D(const std::vector<T> &vector) {
        return *std::min_element(vector.begin(), vector.end());
    }

    template <typename T>
    T min_2D(const std::vector<std::vector<T>> &vector) {
        std::vector<T> minimums = std::vector<T>(vector.size(),0);
        for(int i = 0; i < vector.size(); i++) {
            minimums[i] = min_1D(vector[i]);
        }
        return min_1D(minimums);
    }

    template <typename T>
    T min_3D(const std::vector<std::vector<std::vector<T>>> &vector) {
        std::vector<T> minimums = std::vector<T>(vector.size(),0);
        for(int i = 0; i < vector.size(); i++) {
            minimums[i] = min_2D(vector[i]);
        }
        return min_1D(minimums);
    }

    template <typename T>
    T min_4D(const std::vector<std::vector<std::vector<std::vector<T>>>> &vector) {
        std::vector<T> minimums = std::vector<T>(vector.size(),0);
        for(int i = 0; i < vector.size(); i++) {
            minimums[i] = min_3D(vector[i]);
        }
        return min_1D(minimums);
    }

    template <typename T>
    T Array<T>::get(unsigned long long index) const {
        auto i = index / (this->shape[1]*this->shape[2]*this->shape[3]);
        auto rem = index % (this->shape[1]*this->shape[2]*this->shape[3]);
        auto j = rem / (this->shape[2]*this->shape[3]);
        rem %= (this->shape[2]*this->shape[3]);
        auto k = rem / this->shape[3];
        auto l = rem % this->shape[3];
        return this->data[i][j][k][l];
    }

    template <typename T>
    unsigned long Array<T>::getDimensions() const {
        if(shape[2] == 1 && shape[3] == 1) return 2;
        else return shape.size();
    }

    template <typename T>
    const std::vector<size_t> &Array<T>::getShape() const {
        return shape;
    }

    template <typename T>
    unsigned long long Array<T>::getMax_index() const {
        return this->shape[0]*this->shape[1]*this->shape[2]*this->shape[3];
    }

    /* DATA TRANSFORMATION */

    /* Return value in two complement */
    static inline
    uint16_t profiled_value(float num, int mag, int frac) {
        double scale = pow(2.,(double)frac);
        double intmax = (1u << (mag + frac)) - 1;
        double intmin = -1 * intmax;
        double ds = num * scale;
        if (ds > intmax) ds = intmax;
        if (ds < intmin) ds = intmin;
        auto two_comp = (int)round(ds);
        return (uint16_t)two_comp;
    }

    template <typename T>
    Array<uint16_t> Array<T>::profiled_fixed_point(int mag, int frac) const {
        std::vector<uint16_t> fixed_point_vector;
        for(int i = 0; i < this->shape[0]; i++) {
            for(int j = 0; j < this->shape[1]; j++) {
                for(int k = 0; k < this->shape[2]; k++) {
                    for(int l = 0; l < this->shape[3]; l++) {
                        auto float_value = this->data[i][j][k][l];
                        fixed_point_vector.push_back(profiled_value(float_value,mag,frac));
                    }
                }
            }
        }

        Array<uint16_t> fixed_point_array;
        fixed_point_array.set_values(fixed_point_vector,this->shape);
        return fixed_point_array;
    }

    static inline
    uint16_t tensorflow_value(float num, double scale, float min_value, int max_fixed, int min_fixed) {
        auto sign_mag = (int)(round(num * scale) - round(min_value * scale) + min_fixed);
        sign_mag = std::max(sign_mag, min_fixed);
        sign_mag = std::min(sign_mag, max_fixed);
        return (uint16_t)sign_mag;
    }

    template <typename T>
    Array<uint16_t> Array<T>::tensorflow_fixed_point() const {
        const int NUM_BITS = 8;
        const int max_fixed = 127;
        const int min_fixed = -128;
        const int num_discrete_values = 1u << NUM_BITS;
        const auto range_adjust = num_discrete_values / (num_discrete_values - 1.0);

        std::vector<uint16_t> fixed_point_vector;
        auto min_value = min_4D(this->data);
        auto max_value = max_4D(this->data);
        auto range = (max_value - min_value) * range_adjust;
        auto scale = num_discrete_values / range;

        for(int i = 0; i < this->shape[0]; i++) {
            for(int j = 0; j < this->shape[1]; j++) {
                for(int k = 0; k < this->shape[2]; k++) {
                    for(int l = 0; l < this->shape[3]; l++) {
                        auto float_value = this->data[i][j][k][l];
                        fixed_point_vector.push_back(tensorflow_value(float_value,scale,min_value,max_fixed,min_fixed));
                    }
                }
            }
        }

        Array<uint16_t> fixed_point_array;
        fixed_point_array.set_values(fixed_point_vector,this->shape);
        return fixed_point_array;
    }

    template <typename T>
    void Array<T>::sign_magnitude_representation(int prec) {
        double intmax = (1u << (prec - 1u)) - 1u;
        auto mask = (uint16_t)(intmax + 1);
        for(int i = 0; i < this->shape[0]; i++) {
            for(int j = 0; j < this->shape[1]; j++) {
                for(int k = 0; k < this->shape[2]; k++) {
                    for(int l = 0; l < this->shape[3]; l++) {
                        auto two_comp = (short)this->data[i][j][k][l];
                        auto abs_value = (uint16_t)abs(two_comp);
                        auto sign_mag = abs_value | (two_comp & mask);
                        this->data[i][j][k][l] = sign_mag;
                    }
                }
            }
        }
    }

    template <typename T>
    void Array<T>::powers_of_two_representation(int prec) {
        double intmax = (1u << (prec - 1u)) - 1u;
        auto mask = (uint16_t)(intmax + 1);
        for(int i = 0; i < this->shape[0]; i++) {
            for(int j = 0; j < this->shape[1]; j++) {
                for(int k = 0; k < this->shape[2]; k++) {
                    for(int l = 0; l < this->shape[3]; l++) {
                        auto two_comp = (short)this->data[i][j][k][l];
                        auto abs_value = (uint16_t)abs(two_comp);
                        auto powers_of_two = abs_value & ~mask;
                        this->data[i][j][k][l] = powers_of_two;
                    }
                }
            }
        }
    }

    /* PADDING */

    template <typename T>
    void Array<T>::zero_pad(int padding) {
        auto batch_size = this->shape[0];
        auto act_channels = this->shape[1];
        auto Nx = this->shape[2];
        auto Ny = this->shape[3];

        auto tmp_data4D = Array4D(batch_size, Array3D(act_channels, Array2D(Nx + 2*padding,Array1D(Ny + 2*padding,0))));

        for(int n = 0; n < batch_size; n++) {
            for (int k = 0; k < act_channels; k++) {
                for (int i = 0; i < Nx; i++) {
                    for(int j = 0; j < Ny; j++) {
                        tmp_data4D[n][k][padding + i][padding + j] = this->data[n][k][i][j];
                    }
                }
            }
        }

        this->data.clear();
        this->data = tmp_data4D;
        this->shape.clear();
        this->shape.push_back(batch_size);
        this->shape.push_back(act_channels);
        this->shape.push_back(Nx + 2*padding);
        this->shape.push_back(Ny + 2*padding);
    }

    template <typename T>
    void Array<T>::grid_zero_pad(uint64_t X, uint64_t Y) {
        auto batch_size = this->shape[0];
        auto act_channels = this->shape[1];
        auto Nx = this->shape[2];
        auto Ny = this->shape[3];

        auto tmp_data4D = Array4D(batch_size, Array3D(act_channels, Array2D(X, Array1D(Y,0))));

        for(int n = 0; n < batch_size; n++) {
            for (int k = 0; k < act_channels; k++) {
                for (int i = 0; i < Nx; i++) {
                    for(int j = 0; j < Ny; j++) {
                        tmp_data4D[n][k][i][j] = this->data[n][k][i][j];
                    }
                }
            }
        }

        this->data.clear();
        this->data = tmp_data4D;
        this->shape.clear();
        this->shape.push_back(batch_size);
        this->shape.push_back(act_channels);
        this->shape.push_back((unsigned)X);
        this->shape.push_back((unsigned)Y);
    }

    template <typename T>
    void Array<T>::channel_zero_pad(int K) {
        auto N = this->shape[0];
        auto old_k = this->shape[1];
        auto X = this->shape[2];
        auto Y = this->shape[3];


        auto tmp_data4D = Array4D(N, Array3D(K, Array2D(X, Array1D(Y, 0))));

        for(int n = 0; n < N; n++) {
            for (int k = 0; k < old_k; k++) {
                for (int i = 0; i < X; i++) {
                    for(int j = 0; j < Y; j++) {
                        tmp_data4D[n][k][i][j] = this->data[n][k][i][j];
                    }
                }
            }
        }

        this->data.clear();
        this->data = tmp_data4D;
        this->shape.clear();
        this->shape.push_back(N);
        this->shape.push_back((unsigned)K);
        this->shape.push_back(X);
        this->shape.push_back(Y);
    }

    /* RESHAPE */

    template <typename T>
    void Array<T>::reshape_to_2D() {

        auto batch_size = this->shape[0];
        auto act_channels = this->shape[1] * this->shape[2] * this->shape[3];
        auto tmp_data2D = Array4D(batch_size, Array3D(act_channels, Array2D(1, Array1D(1,0))));

        for(int i = 0; i < this->shape[0]; i++) {
            int count = 0;
            for(int j = 0; j < this->shape[1]; j++) {
                for(int k = 0; k < this->shape[2]; k++) {
                    for(int l = 0; l < this->shape[3]; l++) {
                        tmp_data2D[i][count][0][0] = this->data[i][j][k][l];
                        count++;
                    }
                }
            }
        }

        this->data.clear();
        this->data = tmp_data2D;
        this->shape.clear();
        this->shape.push_back(batch_size);
        this->shape.push_back(act_channels);
        this->shape.push_back(1);
        this->shape.push_back(1);
    }

    template <typename T>
    void Array<T>::split_4D(int K, int X, int Y) {
        auto N = this->shape[0];
        auto old_k = this->shape[1];
        auto old_X = this->shape[2];
        auto old_Y = this->shape[3];

        auto tmp_data4D = Array4D(N, Array3D(K, Array2D(X, Array1D(Y, 0))));

        for(int n = 0; n < N; n++) {
            for (int k = 0; k < old_k; k++) {
                for (int i = 0; i < old_X; i++) {
                    for(int j = 0; j < old_Y; j++) {
                        auto new_k = k / (X*Y);
                        auto rem = k % (X*Y);
                        auto new_i = rem / Y;
                        auto new_j = rem % Y;
                        tmp_data4D[n][new_k][new_i][new_j] = this->data[n][k][i][j];
                    }
                }
            }
        }

        this->data.clear();
        this->data = tmp_data4D;
        this->shape.clear();
        this->shape.push_back(N);
        this->shape.push_back((unsigned)K);
        this->shape.push_back((unsigned)X);
        this->shape.push_back((unsigned)Y);
    }

    template <typename T>
    void Array<T>::reshape_first_layer_act(uint16_t stride) {
        if(getDimensions() != 4 || this->shape[1] != 3) return;
        auto batch_size = this->shape[0];
        auto act_channels = this->shape[1];
        auto Nx = this->shape[2];
        auto Ny = this->shape[3];

        auto new_act_channels = (uint16_t)act_channels*stride*stride;
        auto new_Nx = (uint16_t)ceil(Nx/(double)stride);
        auto new_Ny = (uint16_t)ceil(Nx/(double)stride);

        auto tmp_data4D = Array4D(batch_size, Array3D(new_act_channels, Array2D(new_Nx, Array1D(new_Ny, 0))));

        for(int n = 0; n < batch_size; n++)
            for(int k = 0; k < act_channels; k++)
                for(int i = 0; i < Nx; i++)
                    for(int j = 0; j < Ny; j++) {
                        auto new_i = i/stride;
                        auto new_j = j/stride;
                        auto new_k = (j%stride)*stride*act_channels + act_channels*(i%stride) + k;
                        tmp_data4D[n][new_k][new_i][new_j] = this->data[n][k][i][j];
                    }

        this->data.clear();
        this->data = tmp_data4D;
        this->shape.clear();
        this->shape.push_back(batch_size);
        this->shape.push_back(new_act_channels);
        this->shape.push_back(new_Nx);
        this->shape.push_back(new_Ny);
    }

    template <typename T>
    void Array<T>::reshape_first_layer_wgt(uint16_t stride) {
        if(getDimensions() != 4 || this->shape[1] != 3) return;
        auto num_filters = this->shape[0];
        auto wgt_channels = this->shape[1];
        auto Kx = this->shape[2];
        auto Ky = this->shape[3];

        auto new_wgt_channels = (uint16_t)(uint16_t)wgt_channels*stride*stride;
        auto new_Kx = (uint16_t)ceil(Kx/(double)stride);
        auto new_Ky = (uint16_t)ceil(Ky/(double)stride);

        auto tmp_data4D = Array4D(num_filters, Array3D(new_wgt_channels, Array2D(new_Kx, Array1D(new_Ky, 0))));

        for(int m = 0; m < num_filters; m++)
            for(int k = 0; k < wgt_channels; k++)
                for(int i = 0; i < Kx; i++)
                    for(int j = 0; j < Ky; j++) {
                        auto new_i = i/stride;
                        auto new_j = j/stride;
                        auto new_k = (j%stride)*stride*wgt_channels + wgt_channels*(i%stride) + k;
                        tmp_data4D[m][new_k][new_i][new_j] = this->data[m][k][i][j];
                    }

        this->data.clear();
        this->data = tmp_data4D;
        this->shape.clear();
        this->shape.push_back(num_filters);
        this->shape.push_back(new_wgt_channels);
        this->shape.push_back(new_Kx);
        this->shape.push_back(new_Ky);
    }

    INITIALISE_DATA_TYPES(Array);

}
