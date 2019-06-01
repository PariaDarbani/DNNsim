
#include <interface/NetReader.h>

namespace interface {

    bool ReadProtoFromTextFile(const char* filename, google::protobuf::Message* proto) {
        int fd = open(filename, O_RDONLY);
        auto input = new google::protobuf::io::FileInputStream(fd);
        bool success = google::protobuf::TextFormat::Parse(input, proto);
        delete input;
        close(fd);
        return success;
    }

    template <typename T>
    void NetReader<T>::check_path(const std::string &path) {
        std::ifstream file(path);
        if(!file.good()) {
            throw std::runtime_error("The path " + path + " does not exist.");
        }
    }

    template <typename T>
    core::Layer<T> NetReader<T>::read_layer_caffe(const caffe::LayerParameter &layer_caffe) {
        int Nn = -1, Kx = -1, Ky = -1, stride = -1, padding = -1;

        if(layer_caffe.type() == "Convolution") {
            Nn = layer_caffe.convolution_param().num_output();
            Kx = layer_caffe.convolution_param().kernel_size(0);
            Ky = layer_caffe.convolution_param().kernel_size(0);
            stride = layer_caffe.convolution_param().stride_size() == 0 ? 1 : layer_caffe.convolution_param().stride(0);
            padding = layer_caffe.convolution_param().pad_size() == 0 ? 0 : layer_caffe.convolution_param().pad(0);
        } else if (layer_caffe.type() == "InnerProduct") {
            Nn = layer_caffe.inner_product_param().num_output();
            Kx = 1; Ky = 1; stride = 1; padding = 0;
        } else if (layer_caffe.type() == "LSTM") {
            Nn = layer_caffe.recurrent_param().num_output();
            Kx = 1; Ky = 1; stride = 1; padding = 0;
        }

        std::string name = layer_caffe.name();
        std::replace( name.begin(), name.end(), '/', '-'); // Sanitize name
        std::string type = (name.find("fc") != std::string::npos) ? "InnerProduct" : layer_caffe.type();
        type = (name.find("lstm") != std::string::npos) ? "LSTM" : type;
        type = (name.find("forward") != std::string::npos) ? "LSTM" : type;
        type = (name.find("backward") != std::string::npos) ? "LSTM" : type;
        return core::Layer<T>(type,name,layer_caffe.bottom(0), Nn, Kx, Ky, stride, padding);
    }

    template <typename T>
    core::Network<T> NetReader<T>::read_network_caffe() {
        GOOGLE_PROTOBUF_VERIFY_VERSION;

        std::vector<core::Layer<T>> layers;
        caffe::NetParameter network;

        check_path("models/" + this->name);
        std::string path = "models/" + this->name + "/train_val.prototxt";
        check_path(path);
        if (!ReadProtoFromTextFile(path.c_str(),&network)) {
            throw std::runtime_error("Failed to read prototxt");
        }

        for(const auto &layer : network.layer()) {
           if(this->allowed_layers.find(layer.type()) != this->allowed_layers.end()) {
               layers.emplace_back(read_layer_caffe(layer));
           }
        }

        return core::Network<T>(this->name,layers);
    }

    template <typename T>
    core::Network<T> NetReader<T>::read_network_trace_params() {

        std::vector<core::Layer<T>> layers;

        check_path("models/" + this->name);
        std::string path = "models/" + this->name + "/trace_params.csv";
        check_path(path);

        std::ifstream myfile (path);
        if (myfile.is_open()) {

            std::string line;
            while (getline(myfile,line)) {

                std::vector<std::string> words;
                std::string word;
                std::stringstream ss_line(line);
                while (getline(ss_line,word,','))
                    words.push_back(word);

                std::string type;
                if (words[0].find("decoder") != std::string::npos)
                    type = "Decoder";
                else if (words[0].find("encoder") != std::string::npos)
                    type = "Encoder";
                else if (words[0].find("fc") != std::string::npos || words[0].find("inner_prod") != std::string::npos ||
                        words[0].find("Linear") != std::string::npos)
                    type = "InnerProduct";
                else if (words[0].find("lstm") != std::string::npos)
                    type = "LSTM";
                else
                    type = "Convolution";

                if(words.size() == 7)
                    layers.emplace_back(core::Layer<T>(type,words[0],words[1], stoi(words[2]), stoi(words[3]),
                        stoi(words[4]), stoi(words[5]), stoi(words[6])));
                else if(words.size() == 6)
                    layers.emplace_back(core::Layer<T>(type,words[0],"", stoi(words[1]), stoi(words[2]),
                            stoi(words[3]), stoi(words[4]), stoi(words[5])));
                else
                    throw std::runtime_error("Failed to read trace_params.csv");


            }
            myfile.close();
        }

        return core::Network<T>(this->name,layers);
    }

    template <typename T>
    core::Network<T> NetReader<T>::read_network_conv_params() {

        std::vector<core::Layer<T>> layers;

        check_path("models/" + this->name);
        std::string path = "models/" + this->name + "/conv_params.csv";
        check_path(path);

        std::ifstream myfile (path);
        if (myfile.is_open()) {

            std::string line;
            while (getline(myfile,line)) {

                std::vector<std::string> words;
                std::string word;
                std::stringstream ss_line(line);
                while (getline(ss_line,word,','))
                    words.push_back(word);

                std::string type;
                if(words[2] == "conv")
                    type = "Convolution";
                else if (words[2] == "fc")
                    type = "InnerProduct";
                else if (words[2] == "lstm")
                    type = "LSTM";
                else
                    throw std::runtime_error("Failed to read conv_params.csv");

                auto layer = core::Layer<T>(type,words[1],"",stoi(words[3]), stoi(words[5]), stoi(words[6]),
                        stoi(words[8]), stoi(words[9]));
                layer.setAct_precision(stoi(words[10]),stoi(words[11]),(stoi(words[10])-1)-stoi(words[11]));
                layer.setWgt_precision(16,0,15);
                layers.emplace_back(layer);
            }
            myfile.close();
        }

        return core::Network<T>(this->name,layers);
    }

    template <typename T>
    core::Layer<T> NetReader<T>::read_layer_proto(const protobuf::Network_Layer &layer_proto) {
        core::Layer<T> layer = core::Layer<T>(layer_proto.type(),layer_proto.name(),layer_proto.input(),
        layer_proto.nn(),layer_proto.kx(),layer_proto.ky(),layer_proto.stride(),layer_proto.padding());
        layer.setAct_precision(layer_proto.act_prec(),layer_proto.act_mag(),layer_proto.act_frac());
        layer.setWgt_precision(layer_proto.wgt_prec(),layer_proto.wgt_mag(),layer_proto.wgt_frac());

        // Read weights, activations, and output activations only to the desired layers
        if(this->allowed_layers.find(layer_proto.type()) != this->allowed_layers.end()) {

            std::string type = typeid(T).name() + std::to_string(sizeof(T));// Get template type in run-time

            std::vector<size_t> weights_shape;
            for (const int value : layer_proto.wgt_shape())
                weights_shape.push_back((size_t) value);

            std::vector<size_t> activations_shape;
            for (const int value : layer_proto.act_shape())
                activations_shape.push_back((size_t) value);

            std::vector<T> weights_data;
            std::vector<T> activations_data;


            for (const auto &value : layer_proto.wgt_data_fxd())
                weights_data.push_back(value);

            for (const auto value : layer_proto.act_data_fxd())
                activations_data.push_back(value);

            cnpy::Array<T> weights; weights.set_values(weights_data,weights_shape);
            layer.setWeights(weights);

            cnpy::Array<T> activations; activations.set_values(activations_data,activations_shape);
            layer.setActivations(activations);

        }

        return layer;
    }

    template <typename T>
    core::Network<T> NetReader<T>::read_network_protobuf() {
        GOOGLE_PROTOBUF_VERIFY_VERSION;

        std::vector<core::Layer<T>> layers;
        protobuf::Network network_proto;

        {
            // Read the existing network.
            check_path("net_traces/" + this->name);
            std::string path = "net_traces/" + this->name + "/model.proto";
            check_path(path);
            std::fstream input(path,std::ios::in | std::ios::binary);
            if (!network_proto.ParseFromIstream(&input)) {
                throw std::runtime_error("Failed to read protobuf");
            }
        }

        std::string name = network_proto.name();

        for(const protobuf::Network_Layer &layer_proto : network_proto.layers())
            layers.emplace_back(read_layer_proto(layer_proto));

        return core::Network<T>(this->name,layers);
    }

    template <typename T>
    std::vector<schedule> NetReader<T>::read_schedule_protobuf(const std::string &schedule_type) {
        GOOGLE_PROTOBUF_VERIFY_VERSION;

        protobuf::Schedule network_schedule_proto;

        {
            // Read the existing network.
            check_path("net_traces/" + this->name);
            std::string path = "net_traces/" + this->name + "/schedule_" + schedule_type + ".proto";
            check_path(path);
            std::fstream input(path, std::ios::in | std::ios::binary);
            if (!network_schedule_proto.ParseFromIstream(&input)) {
                throw std::runtime_error("Failed to read protobuf");
            }
        }

        std::vector<schedule> network_schedule;

        for(const auto &schedule_layer_proto : network_schedule_proto.layers()) {
            schedule dense_schedule;
            for(const auto &schedule_time_proto : schedule_layer_proto.times()) {
                time_schedule window_schedule;
                for(const auto &schedule_tuple_proto : schedule_time_proto.tuples()) {
                    schedule_tuple dense_schedule_tuple = std::make_tuple(schedule_tuple_proto.channel(),
                            schedule_tuple_proto.kernel_x(), schedule_tuple_proto.kernel_y(),
                            schedule_tuple_proto.wgt_bits());
                    window_schedule.push_back(dense_schedule_tuple);
                }
                dense_schedule.push_back(window_schedule);
            }
            network_schedule.push_back(dense_schedule);
        }

        return network_schedule;
    }

    template <typename T>
    void NetReader<T>::read_weights_npy(core::Network<T> &network) {
        check_path("net_traces/" + this->name);
        for(core::Layer<T> &layer : network.updateLayers()) {
            std::string file = "/wgt-" + layer.getName() + ".npy" ;
            cnpy::Array<T> weights; weights.set_values("net_traces/" + this->name + file);
            layer.setWeights(weights);
        }
    }

    template <typename T>
    void NetReader<T>::read_activations_npy(core::Network<T> &network) {
        check_path("net_traces/" + this->name);
        for(core::Layer<T> &layer : network.updateLayers()) {
            std::string file = "/act-" + layer.getName() + "-" + std::to_string(batch) + ".npy";
            cnpy::Array<T> activations; activations.set_values("net_traces/" + this->name + file);
            layer.setActivations(activations);
        }
    }

    template <typename T>
    void NetReader<T>::read_training_weights_npy(core::Network<T> &network) {
        check_path("net_traces/" + this->name);
		check_path("net_traces/" + this->name + "/weights");
        for(core::Layer<T> &layer : network.updateLayers()) {
            std::string layer_name = layer.getName();
            if(layer.getType() == "Decoder") {
                auto pos = layer_name.find_last_of('_');
                layer_name = layer_name.substr(0, pos);
            }
            std::string file = "/weights/" + layer_name + "-" + std::to_string(epoch) + "-" +
                   std::to_string(batch) + "-w.npy" ;
            cnpy::Array<T> weights; weights.set_values("net_traces/" + this->name + file);
            layer.setWeights(weights);
        }
    }

    template <typename T>
    void NetReader<T>::read_training_activations_npy(core::Network<T> &network) {
        check_path("net_traces/" + this->name);
		check_path("net_traces/" + this->name + "/input");
        for(core::Layer<T> &layer : network.updateLayers()) {
            std::string file = "/input/" + layer.getName() + "-" + std::to_string(epoch) + "-" +
                    std::to_string(batch) + "-in.npy" ;
            cnpy::Array<T> activations; activations.set_values("net_traces/" + this->name + file);
            layer.setActivations(activations);
        }
    }

    template <typename T>
    void NetReader<T>::read_training_weight_gradients_npy(core::Network<T> &network) {
        check_path("net_traces/" + this->name);
		check_path("net_traces/" + this->name + "/outGrad");
        for(core::Layer<T> &layer : network.updateLayers()) {
            std::string file = "/outGrad/" + layer.getName() + "-" + std::to_string(epoch) + "-" +
                    std::to_string(batch) + "-wGrad.npy" ;
            cnpy::Array<T> weight_gradients; weight_gradients.set_values("net_traces/" + this->name + file);
            layer.setWeightGradients(weight_gradients);
        }
    }

    template <typename T>
    void NetReader<T>::read_training_input_gradients_npy(core::Network<T> &network) {
        check_path("net_traces/" + this->name);
		check_path("net_traces/" + this->name + "/outGrad");
		bool first_layer= true;
        for(core::Layer<T> &layer : network.updateLayers()) {
            if(first_layer) {first_layer = false; continue;}
            std::string file = "/outGrad/" + layer.getName() + "-" + std::to_string(epoch) + "-" +
                    std::to_string(batch) + "-inGrad.npy" ;
            cnpy::Array<T> input_gradients; input_gradients.set_values("net_traces/" + this->name + file);
            layer.setInputGradients(input_gradients);
        }
    }

    template <typename T>
    void NetReader<T>::read_training_output_activation_gradients_npy(core::Network<T> &network) {
        check_path("net_traces/" + this->name);
		check_path("net_traces/" + this->name + "/outGrad");
        for(core::Layer<T> &layer : network.updateLayers()) {
            std::string file = "/outGrad/" + layer.getName() + "-" + std::to_string(epoch) + "-" +
                    std::to_string(batch) + "-outGrad.npy" ;
            cnpy::Array<T> output_gradients; output_gradients.set_values("net_traces/" + this->name + file);
            layer.setOutputGradients(output_gradients);
        }
    }

    template <typename T>
    void NetReader<T>::read_precision(core::Network<T> &network) {

        if(network.isTensorflow_8b()) {
            int i = 0;
            for(core::Layer<T> &layer : network.updateLayers()) {
                layer.setAct_precision(8,7,0);
                layer.setWgt_precision(8,7,0);
                i++;
            }
            return;
        }

        std::string line;
        std::stringstream ss_line;
        std::vector<int> act_mag;
        std::vector<int> act_frac;
        std::vector<int> wgt_mag;
        std::vector<int> wgt_frac;

        std::ifstream myfile ("models/" + this->name + "/precision.txt");
        if (myfile.is_open()) {
            std::string word;

            getline(myfile,line); // Remove first line

            getline(myfile,line);
            ss_line = std::stringstream(line);
            while (getline(ss_line,word,';'))
                act_mag.push_back(stoi(word));

            getline(myfile,line);
            ss_line = std::stringstream(line);
            while (getline(ss_line,word,';'))
                act_frac.push_back(stoi(word));

            getline(myfile,line);
            ss_line = std::stringstream(line);
            while (getline(ss_line,word,';'))
                wgt_mag.push_back(stoi(word));

            getline(myfile,line);
            ss_line = std::stringstream(line);
            while (getline(ss_line,word,';'))
                wgt_frac.push_back(stoi(word));

            myfile.close();

            int i = 0;
            for(core::Layer<T> &layer : network.updateLayers()) {
                layer.setAct_precision(act_mag[i] + act_frac[i],act_mag[i] - 1,act_frac[i]);
                layer.setWgt_precision(wgt_mag[i] + wgt_frac[i],wgt_mag[i] - 1,wgt_frac[i]);
                i++;
            }

        } else {
            // Generic precision
            int i = 0;
            for(core::Layer<T> &layer : network.updateLayers()) {
                layer.setAct_precision(16,13,2);
                layer.setWgt_precision(16,0,15);
                i++;
            }
        }
    }

    INITIALISE_DATA_TYPES(NetReader);

}
