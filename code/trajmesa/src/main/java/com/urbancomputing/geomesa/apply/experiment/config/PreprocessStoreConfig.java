package com.urbancomputing.geomesa.apply.experiment.config;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.datatype.jsr310.JavaTimeModule;
import com.urbancomputing.geomesa.io.config.input.InputConfig;
import com.urbancomputing.geomesa.model.conf.IDataConfig;

import java.io.IOException;
import java.io.Serializable;
import java.net.URL;

public class PreprocessStoreConfig implements Serializable {
    /**
     * ObjectMapper
     */
    private final static ObjectMapper MAPPER = new ObjectMapper().registerModule(new JavaTimeModule());

    /**
     * trajectory data inputConfig(hdfs)
     */
    @JsonProperty
    private InputConfig inputConfig;
    /**
     * trajectory data schema config
     */
    @JsonProperty
    private IDataConfig dataConfig;
    /**
     * trajectory preprocess parameters config
     */
    @JsonProperty
    private PreprocessConfig preprocessConfig;
    /**
     * trajectory storeConfig
     */
    @JsonProperty
    private StoreConfig storeConfig;

    public InputConfig getInputConfig() {
        return inputConfig;
    }

    public IDataConfig getDataConfig() {
        return dataConfig;
    }

    public PreprocessConfig getPreprocessConfig() {
        return preprocessConfig;
    }

    public StoreConfig getStoreConfig() {
        return storeConfig;
    }

    /**
     * @param raw json string
     * @return : com.urbancomputing.geomesa.apply.std.config.PreprocessStoreConfig
     **/
    public static PreprocessStoreConfig parse(String raw) throws IOException {
        return MAPPER.readValue(raw, PreprocessStoreConfig.class);
    }

    /**
     * @param url url of json config file
     * @return : com.urbancomputing.geomesa.apply.std.config.PreprocessStoreConfig
     **/
    public static PreprocessStoreConfig parse(URL url) throws IOException {
        return MAPPER.readValue(url, PreprocessStoreConfig.class);
    }
}
