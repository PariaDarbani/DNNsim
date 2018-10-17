# DNNsim 0.0.2

Gitignore is set up for CLion IDE, if you want to use a different IDE add their project file extensions to .gitignore. 
It can be used from the command line. The folder specified for models under .gitignore is "models". 
All the inputs/outputs files of a network architecture must be in the same folder in case of Trace input. Example: models/alexnet/{Inputs/Outputs}

#### TODO list
*   Add positional parameter for transform or simulate
*   Inference
*   Test other networks

#### Compilation:
Command line compilation. First we need to configure the project (It can be Debug or Release for optimizations):
    
    cmake -H. -Bcmake-build-debug -DCMAKE_BUILD_TYPE=Debug

Then, we can proceed to build the project

    cmake --build cmake-build-debug/ --target all

Finally, we can execute it as:

    ./cmake-build-debug/bin/DNNsim -n <Name> -i <Path_Input> --itype <Trace|Protobuf|Gzip> -o <Path_Ouput> --otype <Protobuf|Gzip>

#### Structure:
*   **cnpy**: Folder for supporting math libraries
    *   cnpy: library to read Numpy arrays
    *   Array: class to store and operate with flatten arrays
*   **core**: Folder for the main classes of the simulator
    *   Network: class to store the network
    *   Layer: class to store the layer of the network
    *   Simulator: 
*   **interface**: Folder to interface with input/output operations
    *   NetReader: class to read and load a network using different formats
    *   NetWriter: class to write and dump a network using different formats
    * proto: Folder for protobuf definition
        * network.proto Google protobuf definition for the network
        
#### Requeriments
*   Cmake posterior to version 3.10
*   Google Protobuf for C++. Installation link:
    *   https://github.com/protocolbuffers/protobuf/blob/master/src/README.md

#### Allowed input files

*   The architecture of the net in a trace.csv file (without weights and activations)
*   Weights, Inputs and outputs activations in a *.npy file using the following format:
    *   wgt-$NAME.npy | act-$NAME-0{-out}.npy
*   Full network in a Google protobuf format file
*   Full network in a Gzip Google protobuf format

#### Allowed output files

*   Full network in a Google protobuf format file
*   Full network in a Gzip Google protobuf format


[//]:<> (Current python simulator for Bit-Pragmatic is under:) 
[//]:<> (*   /aenao-99/delmasl1/cnvlutin-PRA/MIsim/functionalSerial.py)
[//]:<> (*   /aenao-99/delmasl1/cnvlutin-PRA/MIsim/testSystem.py)


