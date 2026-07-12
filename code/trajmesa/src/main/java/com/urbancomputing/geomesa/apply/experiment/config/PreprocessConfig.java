package com.urbancomputing.geomesa.apply.experiment.config;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.urbancomputing.geomesa.process.preprocess.noisefiltering.params.BaseFilterParams;
import com.urbancomputing.geomesa.process.preprocess.segmentation.SegmenterParams;

public class PreprocessConfig {
    /**
     * filter parameters
     */
    @JsonProperty
    private BaseFilterParams filterParams;
    /**
     * segment parameters
     */
    @JsonProperty
    private SegmenterParams segmenterParams;

    public BaseFilterParams getFilterParams() {
        return filterParams;
    }

    public SegmenterParams getSegmenterParams() {
        return segmenterParams;
    }

}
