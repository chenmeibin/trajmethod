package com.urbancomputing.geomesa.apply.std.config;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.core.JsonParseException;
import com.fasterxml.jackson.databind.JsonMappingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.urbancomputing.geomesa.model.conf.IDataConfig;
import com.urbancomputing.geomesa.io.config.input.InputConfig;
import com.urbancomputing.geomesa.io.config.output.OutputConfig;

import java.io.IOException;
import java.net.URL;

/**
 * @author : hehuajun3
 * @description :
 * @date : Created in 2019-04-30 18:42
 * @modified by :
 **/
public class DataLoaderConfig {
    /**
     * ObjectMapper
     */
    private final static ObjectMapper MAPPER = new ObjectMapper();
    /**
     * 数据加载配置
     */
    @JsonProperty
    private InputConfig inputConfig;
    /**
     * 数据映射
     */
    @JsonProperty
    private IDataConfig dataConfig;
    /**
     * 输出配置
     */
    @JsonProperty
    private OutputConfig outputConfig;

    public InputConfig getInputConfig() {
        return inputConfig;
    }

    public IDataConfig getDataConfig() {
        return dataConfig;
    }

    public OutputConfig getOutputConfig() {
        return outputConfig;
    }

    /**
     * 数据同步
     *
     * @param raw 原始字符串
     * @return : com.urbancomputing.geomesa.apply.std.config.DataLoaderConfig
     **/
    public static DataLoaderConfig parse(String raw) throws JsonParseException {
        try {
            return MAPPER.readValue(raw, DataLoaderConfig.class);
        } catch (JsonMappingException e) {
            throw new IllegalArgumentException(e.toString());
        } catch (JsonParseException e) {
            throw e;
        } catch (IOException e) {
            throw new RuntimeException();
        }
    }

    /**
     * 数据同步
     *
     * @param url url
     * @return : com.urbancomputing.geomesa.apply.std.config.DataLoaderConfig 地图匹配的配置
     **/
    public static DataLoaderConfig parse(URL url) throws IOException {
        return MAPPER.readValue(url, DataLoaderConfig.class);
    }
}
