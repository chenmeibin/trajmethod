package com.urbancomputing.geomesa.apply.experiment.config;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.urbancomputing.geomesa.io.config.output.OutputConfig;

public class StoreConfig {
    /**
     * trajectory outputConfig(geoMesa)
     */
    @JsonProperty
    private OutputConfig outputConfig;
    /**
     * is trajectory stored as horizontal geoMesa schema
     */
    @JsonProperty
    private boolean storeList = true;
    /**
     * is trajectory stored as vertical geoMesa schema
     */
    @JsonProperty
    private boolean storePoint = true;

    public OutputConfig getOutputConfig() {
        return outputConfig;
    }

    public boolean isStorePoint() {
        return storePoint;
    }

    public boolean isStoreList() {
        return storeList;
    }

}
