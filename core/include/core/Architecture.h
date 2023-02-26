#ifndef DNNSIM_ARCHITECTURE_H
#define DNNSIM_ARCHITECTURE_H

#include <sys/Stats.h>
#include "Utils.h"
#include <bits/stdc++.h>



namespace core {

    /**
     * Generic Architecture
     * @tparam T Data type values
     */
    template <typename T>
    class Architecture {

    protected:

        /* SIMULATION PARAMETERS */

        /** Column index */
        uint64_t column_index = 0;

        /** Column cycles */
        std::vector<uint64_t> column_cycles;

        /** Activations precision */
        int act_prec = 0;

        /** Weights precision */
        int wgt_prec = 0;

        /** Number of activation steps */
        int act_blks = 0;

        /** Number of weight steps */
        int wgt_blks = 0;

        /** Network width */
        int network_width = 0;

        /** True if signed activations */
        bool signed_act = false;

        /** True if signed weights */
        bool signed_wgt = false;

        /** Linear layer */
        bool linear = false;

        /** Global cycle */
        std::shared_ptr<uint64_t> global_cycle;

        /** Ready cycle */
        uint64_t ready_cycle = 0;

        /** Done cycle */
        uint64_t done_cycle = 0;

        /* STATISTICS */

        /** Compute cycles */
        std::vector<uint64_t> compute_cycles;

        /** Number of cycles */
        uint64_t cycles = 0;

        /** Scheduled PEs */
        uint64_t scheduled_pe = 0;

        //paria
        uint64_t allPEsofoneTileClocked = 0;

        /** Idle PEs */
        uint64_t idle_pe = 0;

    public:

        //Paria ba bardashtane const inha ra az halate read-only dar avardam
        /** Idle Lanes */
        uint64_t idle_lane = 0;

        /** Idle Lanes */
        uint64_t idle_PE_PARIA = 0;
        uint64_t idle_PE_and_Tile_PARIA = 0;

        /** Number of concurrent multiplications per PE */
        uint32_t LANES = 0;

        /** Number of columns */
        uint32_t COLUMNS = 0;

        /** Number of rows */
        uint32_t ROWS = 0;

        /** Number of tiles */
        const uint32_t TILES = 0;

        /** PE bit-width */
        const uint32_t PE_WIDTH = 0;

        //paria
        float confArr[500][3];
        int configsNumber = 0;

        /**
         * Constructor
         */
        Architecture() = default;

        /**
         * Constructor
         * @param _LANES    Number of concurrent multiplications per PE
         * @param _COLUMNS  Number of columns
         * @param _ROWS     Number of rows
         * @param _TILES    Number of tiles
         * @param _PE_WIDTH    Bits per PE
         */
        Architecture(uint32_t _LANES, uint32_t _COLUMNS, uint32_t _ROWS, uint32_t _TILES, uint32_t _PE_WIDTH) :
                LANES(_LANES), COLUMNS(_COLUMNS), ROWS(_ROWS), TILES(_TILES), PE_WIDTH(_PE_WIDTH) {}

        /**
         * Return the number of lanes
         * @return Lanes
         */
        uint32_t getLanes() const {
            return LANES;
        }

        /**
         * Return the number of columns
         * @return Columns
         */
        uint32_t getColumns() const {
            return COLUMNS;
        }

        /**
         * Return the number of rows
         * @return Rows
         */
        uint32_t getRows() const {
            return ROWS;
        }

        /**
         * Return the number of tiles
         * @return Tiles
         */
        uint32_t getTiles() const {
            return TILES;
        }

        /**
         * Return the pe bit-width
         * @return PE width
         */
        uint32_t getPeWidth() const {
            return PE_WIDTH;
        }

        /* AUXILIARY FUNCTIONS */

        /**
         * Initialise layer
         * @param _act_prec      Activations precision
         * @param _wgt_prec      Weights precision
         * @param _act_blks      Activation steps
         * @param _wgt_blks      Weight steps
         * @param _network_width Network width
         * @param _signed_act    Signed activations
         * @param _signed_wgt    Signed weights
         * @param _linear        Linear layer
         * @param EF_COLUMNS     Number of effective columns
         */
        virtual void configure_layer(int _act_prec, int _wgt_prec, int _act_blks, int _wgt_blks, int _network_width,
                                     bool _signed_act, bool _signed_wgt, bool _linear, uint64_t EF_COLUMNS) {
            act_prec = _act_prec;
            wgt_prec = _wgt_prec;
            act_blks = _act_blks;
            wgt_blks = _wgt_blks;
            network_width = _network_width;
            signed_act = _signed_act;
            signed_wgt = _signed_wgt;
            linear = _linear;

            ready_cycle = 0;
            done_cycle = 0;

            column_cycles = _linear ? std::vector<uint64_t>(EF_COLUMNS, 0) :
                            std::vector<uint64_t>(EF_COLUMNS * _act_blks, 0);
            column_index = 0;

            compute_cycles = _linear ? std::vector<uint64_t>(EF_COLUMNS, 0) :
                             std::vector<uint64_t>(EF_COLUMNS * _act_blks, 0);
            cycles = 0;
            scheduled_pe = 0;
            idle_pe = 0;

            allPEsofoneTileClocked = 0;
        }

        /**
         * Set shared global cycle
         * @param globalCycle
         */
        void setGlobalCycle(const std::shared_ptr<uint64_t> &globalCycle) {
            global_cycle = globalCycle;
        }

        /**
         * Return number of cycles
         * @return Cycles
         */
        virtual uint64_t getCycles() const {
            return cycles;
        }

        /**
         * Return scheduled PEs
         * @return Scheduled PEs
         */
        uint64_t getScheduledPe() const {
            return scheduled_pe;
        }

        uint64_t getallPEsclocked() const {
            return allPEsofoneTileClocked;
        }

        /**
         * Return idle PEs
         * @return Idle PEs
         */
        uint64_t getIdlePe() const {
            return idle_pe;
        }

        /**
         * Get idle Lanes
         * @return Idle Lanes
         */
        uint64_t getIdleLane() const {
            return idle_lane;
        }

        uint64_t getIdlePE_PARIA() const {
            return idle_PE_PARIA;
        }

        uint64_t primaryRow() {
            float primaryRow = ROWS;
            return primaryRow;
        }

        uint64_t primaryColumn() {
            float primaryColumn = COLUMNS;
            return primaryColumn;
        }

        uint64_t primaryLane() {
            float primaryLane = LANES;
            return primaryLane;
        }

        /**
         * Return name of the class
         * @return Name
         */
        virtual std::string name() = 0;

        /**
         * Convert the data representation to the one need it.
         * @param data          Array of values
         */
        virtual void dataConversion(base::Array<T> &data) {}

        /* CYCLES */

        /**
         * Return stats filename for the architecture in the cycles function
         * @return Filename
         */
        virtual std::string filename() {
            return "_L" + std::to_string(LANES) + "_C" + std::to_string(COLUMNS) + "_R" + std::to_string(ROWS) +
                   "_T" + std::to_string(TILES) + "_BP" + std::to_string(PE_WIDTH);
        }


        /**
         * Return stats header for the architecture in the cycles function
         * @return Header
         */
        virtual std::string header() {
            std::string header = "Number of lanes/terms per PE: " + std::to_string(LANES) + "\n";
            header += "Number of columns/windows in parallel: " + std::to_string(COLUMNS) + "\n";
            header += "Number of rows/filters in parallel: " + std::to_string(ROWS) + "\n";
            header += "Number of tiles: " + std::to_string(TILES) + "\n";
            header += "PE input bit-width: " + std::to_string(PE_WIDTH) + "\n";
            return header;
        }

        /**
         * Return if calculate deltas for the window buffer
         * @return True if diffy, False if not
         */
        virtual bool diffy() = 0;

        /**
         * Return if schedule the weight buffer
         * @return True if weight buffer to schedule, False if not
         */
        virtual bool schedule() = 0;

        /**
         * Calculate cycles for all the tiles
         * @param tiles_data Processing information for all the tiles
         */
        virtual void process_tiles(const std::shared_ptr<TilesData < T>>

        &tiles_data) = 0;

        /**
         * Return true if ready to feed need data
         * @return True if ready to process data
         */
        virtual bool ready() { return ready_cycle <= *global_cycle; }

        /**
         * Return true if processing is done
         * @return True if done
         */
        virtual bool done() { return done_cycle <= *global_cycle; }

        /**
         * Return true if processing has finished
         * @return True if done
         */
        virtual bool flush() { return done_cycle <= *global_cycle; }

        /* POTENTIALS */

        /**
         * Return stats filename for the architecture in the potentials function
         * @return Filename
         */
        virtual std::string filename_pot() = 0;

        /**
         * Return stats header for the architecture in the potentials function
         * @return Header
         */
        virtual std::string header_pot() = 0;

        /** Compute number of one bit multiplications given a weights and an activation
         * @param act   Activation
         * @param wgt   Weight
         * @return      Number of one bit multiplications
         */
        virtual uint16_t computeBits(T act, T wgt) = 0;


        // Function to display the array
        int divisions[91][3];
        int ii = 0;

        bool searchArray(float arr[500][3], float a, float b, float c) {
            for (int i = 0; i < 500; i++) {
                if (arr[i][0] == a) {
                    if (arr[i][1] == b) {
                        if (arr[i][2] == c)
                            return true;
                    }
                }
            }
            return false;
        }

        void searchArrayOneD (float arr[100], float a)
        {
            printf("Best configs:");
            for (int i = 0; i < configsNumber; i++)
            {
                        if (arr[i] == a)
                        {
                            printf("\n ROW:  %.6g", (confArr[i][0]));
                            printf(" COLUMN:  %.6g ", (confArr[i][1]));
                            printf(" LANE:  %.6g",(confArr[i][2]));
                        }
            }
        }

        void availableConfigs (bool RowConfig, bool ColConfig)
        {
            int basicPElanes = 16;
            int startLanes = log2 (basicPElanes);

            if (RowConfig == true && ColConfig == true)
            {
                for (int configRow = 0; configRow < ((ROWS * COLUMNS * LANES) / basicPElanes) + 1; configRow++) {
                    for (int configColumn = 0; configColumn < ((ROWS * COLUMNS * LANES) / basicPElanes) + 1; configColumn++) {
                        for (int configLane = startLanes; configLane < ((ROWS * COLUMNS * LANES) / basicPElanes) + 1; configLane++) { //minimum lanes of each pe is 16
                            if ((pow(2, configRow) * pow(2, configColumn) * pow(2, configLane) == (ROWS * COLUMNS * LANES))
                                && !searchArray(confArr, pow(2, configRow), pow(2, configColumn), pow(2, configLane))
                                && ((pow(2, configRow) == ROWS) || (pow(2, configColumn) == COLUMNS))
                                && !(ROWS > 1 && COLUMNS > 1 && pow(2, configRow) == 1 && pow(2, configColumn) == 1)//kole tile yek pe nashavad
                                //&& ( pow(2, configLane) != 32 )
                                )
                            {
                                confArr[configsNumber][0] = pow(2, configRow);
                                confArr[configsNumber][1] = pow(2, configColumn);
                                confArr[configsNumber][2] = pow(2, configLane);
                                configsNumber++;
                            }
                        }
                    }
                }
            }
            else if ( RowConfig == false && ColConfig == true )
            {
                    for (int configColumn = 0; configColumn < ((COLUMNS * LANES) / basicPElanes) + 1; configColumn++) {
                        for (int configLane = 4; configLane < ((COLUMNS * LANES) / basicPElanes) + 1; configLane++) { //minimum lanes of each pe is 16
                            if ((pow(2, configColumn) * pow(2, configLane) == (COLUMNS * LANES))
                                && !searchArray(confArr, ROWS,pow(2, configColumn), pow(2, configLane))
                                //&& ( pow(2, configLane) != 32 ))
                            {
                                confArr[configsNumber][0] = ROWS;
                                confArr[configsNumber][1] = pow(2, configColumn);
                                confArr[configsNumber][2] = pow(2, configLane);
                                configsNumber++;
                            }
                        }
                    }
            }
            else if ( RowConfig == true && ColConfig == false )
            {
                memset(confArr, 0, sizeof(confArr[0][0]) * 500 * 3);
                for (int configLane = startLanes; configLane < ((ROWS * LANES) / basicPElanes) + 1; configLane++) { //minimum lanes of each pe is 16
                    for (int configRow = 0; configRow < ((ROWS * LANES) / basicPElanes) + 1; configRow++) {
                        if ((pow(2, configRow) * pow(2, configLane) == (ROWS * LANES))
                            && !searchArray(confArr, pow(2, configRow), COLUMNS, pow(2, configLane))
                            //&&( pow(2, configLane) != 128 )
                            //&&( pow(2, configLane) != 256 )
                            //&&( pow(2, configLane) != 32 )
                            //&&( pow(2, configLane) != 64 )
                            )
                        {
                            confArr[configsNumber][0] = pow(2, configRow);
                            confArr[configsNumber][1] = COLUMNS;
                            confArr[configsNumber][2] = pow(2, configLane);
                            configsNumber++;
                        }
                    }
                }
            }
        }

        void pariaApproach(float act_channels, float output_windows, float num_filters, int layer_it, bool RowConfig, bool ColConfig) {

                //test
                //ROWS = 2;
                //COLUMNS = 2;
                //LANES = 2;

                float oldComputeCycle = ceil((float) num_filters / (ROWS * TILES)) * //ROW
                                        ceil((float) output_windows / COLUMNS) * //Columnn
                                        ceil((float) act_channels / LANES); //Lane

                if (layer_it == 0)
                {
                    availableConfigs(RowConfig, ColConfig);
                }

                float allResultArr[configsNumber];

                for (int j = 0; j < configsNumber; j++) {
                        allResultArr[j] = //computing cycles of all different configs
                                ceil((float) num_filters / (confArr[j][0] * TILES)) * //ROW
                                ceil((float) output_windows / confArr[j][1]) * //Columnn
                                ceil((float) act_channels / confArr[j][2]) ; //Lane
                                                         }

                float temp = oldComputeCycle;  //computingCycle
                int smallestIndex = 987654321; //for oldcycle

                for (int p = 0; p < configsNumber; p++)
                {
                    if (temp > allResultArr[p])
                    {
                                //if (!((ceil((float) act_channels / confArr[p][2]) * confArr[p][2]) > (ceil((float) act_channels / LANES) * LANES)))
                        if (
                                !(
                                 (ceil((float) num_filters / (confArr[p][0])) * ceil((float) act_channels / confArr[p][2]))
                        >
                                 (ceil((float) num_filters / ROWS)            * ceil((float) act_channels / LANES))
                                 ))
                                {
                                    temp = allResultArr[p];
                                    smallestIndex = p;
                                }
                    }
                }

                //all best configs
               searchArrayOneD(allResultArr, temp);
            //float    smallestConfssss[sizeof(smallestConfs)] = smallestConfs;

                if ( smallestIndex != 987654321)
                {
                    ROWS = confArr[smallestIndex][0];
                    COLUMNS = confArr[smallestIndex][1];
                    LANES = confArr[smallestIndex][2];
                }

                //std::fill_n(allResultArr, configsNumber, 0);
                memset(allResultArr, 0, configsNumber);
                //memset(confArr, 0, sizeof(confArr[0][0]) * 500 * 3);
        }

        uint64_t UnusedPEs(float act_channels, float outputWindows, float num_filters, float Kx, float Ky)//ghalat
        {
//test
            //outputWindows = 1;
            //num_filters = 1000;


            float LayerusedPE = 0;
            float LayerUnusedPE = 0;
            int usedCalc = 0;
            float TotalPEs = COLUMNS * ROWS;
            //auto iterations = ceil(outputWindows / COLUMNS) * ceil( act_channels / LANES) * ceil( num_filters / (ROWS * TILES)) * Kx * Ky;
            //int tempChannels = act_channels;
            int tempWindows = outputWindows;
            int tempFilters = num_filters;

           // for (int i = 0; i < iterations; i++)
            if (outputWindows == 1) {
                while (tempFilters > 0) {
                    while (tempWindows > 0) {


                        if (tempFilters > ROWS) {

                            usedCalc = (int) (tempWindows * ROWS);
                            LayerUnusedPE += (ROWS) - usedCalc;
                            tempWindows = 0;
                            //tempFilters -= ROWS;
                        } else {
                            usedCalc = (int) (tempWindows * tempFilters);
                            LayerUnusedPE += (ROWS) - usedCalc;
                            //continue;
                            tempWindows = 0;
                            //tempFilters = 0;
                        }
                    }
                    tempWindows = outputWindows;
                    if (tempFilters > ROWS) {
                        tempFilters -= ROWS;
                    } else {
                        tempFilters = 0;
                    }
                }
            } else {
                while (tempFilters > 0) {
                    while (tempWindows > 0) {


                        if (tempFilters > ROWS) {
                            if (tempWindows > COLUMNS) {

                                usedCalc = (int) (COLUMNS * ROWS);
                                LayerUnusedPE += (ROWS * COLUMNS) - usedCalc;
                                tempWindows -= COLUMNS;
                                //tempFilters -= ROWS;
                            } else {
                                usedCalc = (int) (tempWindows * ROWS);
                                LayerUnusedPE += (ROWS * COLUMNS) - usedCalc;
                                tempWindows = 0;
                                //tempFilters -= ROWS;
                            }
                        } else {
                            if (tempWindows > COLUMNS) {

                                usedCalc = (int) (COLUMNS * tempFilters);
                                LayerUnusedPE += (ROWS * COLUMNS) - usedCalc;
                                tempWindows -= COLUMNS;
                                //tempFilters = 0;

                            } else {

                                usedCalc = (int) (tempWindows * tempFilters);
                                LayerUnusedPE += (ROWS * COLUMNS) - usedCalc;
                                //continue;
                                tempWindows = 0;
                                //tempFilters = 0;
                            }
                        }
                    }
                    tempWindows = outputWindows;
                    if (tempFilters > ROWS) {
                        tempFilters -= ROWS;
                    } else {
                        tempFilters = 0;
                    }
                }
            }
            return idle_PE_PARIA = LayerUnusedPE * ceil( act_channels / LANES) * Kx * Ky;
        }

        uint64_t UnusedPEsandTiles(float act_channels, float outputWindows, float num_filters, float Kx, float Ky)//ghalat
        {
//test
            //outputWindows = 1;
            //num_filters = 1000;


            float LayerusedPE = 0;
            float LayerUnusedPE = 0;
            int usedCalc = 0;
            float TotalPEs = COLUMNS * ROWS * TILES;
            //auto iterations = ceil(outputWindows / COLUMNS) * ceil( act_channels / LANES) * ceil( num_filters / (ROWS * TILES)) * Kx * Ky;
            //int tempChannels = act_channels;
            int tempWindows = outputWindows;
            int tempFilters = num_filters;

            // for (int i = 0; i < iterations; i++)
            if (outputWindows == 1) {
                while (tempFilters > 0) {
                    while (tempWindows > 0) {


                        if (tempFilters > ROWS * TILES) {

                            usedCalc = (int) (tempWindows * ROWS * TILES);
                            LayerUnusedPE += (ROWS * TILES) - usedCalc;
                            tempWindows = 0;
                            //tempFilters -= ROWS;
                        } else {
                            usedCalc = (int) (tempWindows * tempFilters);
                            LayerUnusedPE += (ROWS * TILES) - usedCalc;
                            //continue;
                            tempWindows = 0;
                            //tempFilters = 0;
                        }
                    }
                    tempWindows = outputWindows;
                    if (tempFilters > ROWS * TILES) {
                        tempFilters -= ROWS * TILES;
                    } else {
                        tempFilters = 0;
                    }
                }
            } else {
                while (tempFilters > 0) {
                    while (tempWindows > 0) {


                        if (tempFilters > ROWS * TILES) {
                            if (tempWindows > COLUMNS) {

                                usedCalc = (int) (COLUMNS * ROWS * TILES);
                                LayerUnusedPE += (ROWS  * TILES * COLUMNS) - usedCalc;
                                tempWindows -= COLUMNS;
                                //tempFilters -= ROWS;
                            } else {
                                usedCalc = (int) (tempWindows * ROWS * TILES);
                                LayerUnusedPE += (ROWS  * TILES * COLUMNS) - usedCalc;
                                tempWindows = 0;
                                //tempFilters -= ROWS;
                            }
                        } else {
                            if (tempWindows > COLUMNS) {

                                usedCalc = (int) (COLUMNS * tempFilters);
                                LayerUnusedPE += (ROWS  * TILES * COLUMNS) - usedCalc;
                                tempWindows -= COLUMNS;
                                //tempFilters = 0;

                            } else {

                                usedCalc = (int) (tempWindows * tempFilters);
                                LayerUnusedPE += (ROWS * TILES * COLUMNS) - usedCalc;
                                //continue;
                                tempWindows = 0;
                                //tempFilters = 0;
                            }
                        }
                    }
                    tempWindows = outputWindows;
                    if (tempFilters > ROWS * TILES) {
                        tempFilters -= ROWS * TILES;
                    } else {
                        tempFilters = 0;
                    }
                }
            }
            return idle_PE_and_Tile_PARIA = LayerUnusedPE * ceil( act_channels / LANES) * Kx * Ky;
        }

        void UnusedLanes(float act_channels, float outputWindows, float num_filters, float Kx, float Ky)
        {
            //test
/*
        act_channels = 3;
        outputWindows = 4;
        num_filters = 3;
        Kx = 1;
        Ky = 1;

        N_LANES = 4;
        N_COLUMNS = 3;
        N_ROWS = 2;
        N_TILES = 3;
*/

            float LayerUnusedMultipliers = 0;
            int UnusedCalc = 0;
            float TotalMultipliers = LANES * COLUMNS * ROWS * TILES;
            auto iterations = ceil(outputWindows / COLUMNS) * ceil( act_channels / LANES) * ceil( num_filters / (ROWS * TILES));
            int tempChannels = act_channels;
            int tempWindows = outputWindows;
            int tempFilters = num_filters;

            for (int i = 0; i < iterations; i++)
            {
                if (tempFilters > ROWS * TILES) {
                    if (tempWindows > COLUMNS) {
                        if (tempChannels > LANES) {

                            UnusedCalc = (int) (TotalMultipliers - (LANES * COLUMNS * ROWS * TILES));
                            tempChannels -= LANES;

                        } else {

                            UnusedCalc = (int) (TotalMultipliers - (tempChannels * COLUMNS * ROWS * TILES));
                            tempWindows -= COLUMNS;
                            tempChannels = act_channels;
                        }
                    } else {
                        if (tempChannels > LANES) {

                            UnusedCalc = (int) (TotalMultipliers - (LANES * tempWindows * ROWS * TILES));
                            tempChannels -= LANES;

                        } else {

                            UnusedCalc = (int) (TotalMultipliers - (tempChannels * tempWindows * ROWS * TILES));
                            tempChannels = act_channels;
                            tempWindows = outputWindows;
                            tempFilters -= ROWS * TILES;
                        }
                    }
                } else {
                    if (tempWindows > COLUMNS) {
                        if (tempChannels > LANES) {

                            UnusedCalc = (int) (TotalMultipliers - (LANES * COLUMNS * tempFilters));
                            tempChannels -= LANES;

                        } else {

                            UnusedCalc = (int) (TotalMultipliers - (tempChannels * COLUMNS * tempFilters));
                            tempWindows -= COLUMNS;
                            tempChannels = act_channels;
                        }
                    } else {
                        if (tempChannels > LANES) {

                            UnusedCalc = (int) (TotalMultipliers - (LANES * tempWindows * tempFilters));
                            tempChannels -= LANES;

                        } else {

                            UnusedCalc = (int) (TotalMultipliers - (tempChannels * tempWindows * tempFilters));
                            tempChannels = act_channels;
                            tempWindows = outputWindows;
                            tempFilters = num_filters;
                        }
                    }
                }
                LayerUnusedMultipliers += UnusedCalc;
            }

            idle_lane = LayerUnusedMultipliers * Kx * Ky;
        }
    };

}

#endif //DNNSIM_ARCHITECTURE_H
