package com.urbancomputing.geomesa.apply.std.config;

import com.fasterxml.jackson.annotation.JsonCreator;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.core.JsonParseException;
import com.fasterxml.jackson.databind.JsonMappingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.datatype.jsr310.JavaTimeModule;
import com.urbancomputing.geomesa.process.preprocess.noisefiltering.params.BaseFilterParams;
import com.urbancomputing.geomesa.model.conf.IDataConfig;
import com.urbancomputing.geomesa.io.config.input.InputConfig;
import com.urbancomputing.geomesa.io.config.output.OutputConfig;

import java.io.IOException;
import java.io.Serializable;
import java.net.URL;

/**
 * @description: 轨迹去噪config
 * @author: He Huajun
 * @date : 2019-03-16 16:01
 **/
public class NoiseConfig implements Serializable {
    /**
     *
     */
    private final static ObjectMapper MAPPER = new ObjectMapper()
            .registerModule(new JavaTimeModule());
    /**
     * 数据加载配置
     */
    private InputConfig inputConfig;
    /**
     * 数据映射
     */
    private IDataConfig dataConfig;
    /**
     * 过滤参数
     */
    private BaseFilterParams filterParams;
    /**
     * 输出配置
     */
    private OutputConfig outputConfig;

    /**
     * NoiseConfig的构造函数
     */
    @JsonCreator
    public NoiseConfig(@JsonProperty("inputConfig") InputConfig inputConfig,
                       @JsonProperty("dataConfig") IDataConfig dataConfig,
                       @JsonProperty("filterParams") BaseFilterParams filterParams,
                       @JsonProperty("outputConfig") OutputConfig outputConfig ) {
        this.inputConfig = inputConfig;
        this.dataConfig = dataConfig;
        this.filterParams = filterParams;
        this.outputConfig = outputConfig;
    }

    public InputConfig getInputConfig() {
        return inputConfig;
    }

    public IDataConfig getDataConfig() {
        return dataConfig;
    }

    public BaseFilterParams getFilterParams() {
        return filterParams;
    }

    public OutputConfig getOutputConfig() {
        return outputConfig;
    }

    /**
     * 从字符串中解析NoiseConfig
     *
     * @param raw 原始字符串
     * @return : com.urbancomputing.geomesa.apply.std.config.NoiseConfig 去噪配置
     **/
    public static NoiseConfig parse(String raw) throws JsonParseException {
        try {
            return MAPPER.readValue(raw, NoiseConfig.class);
        } catch (JsonMappingException e) {
            throw new IllegalArgumentException(e.toString());
        } catch (JsonParseException e) {
            throw e;
        } catch (IOException e) {
            throw new RuntimeException();
        }
    }

    /**
     * 解析
     *
     * @param url url
     * @return : com.urbancomputing.geomesa.apply.std.config.NoiseConfig 去噪配置
     **/
    public static NoiseConfig parse(URL url) throws IOException {
        return MAPPER.readValue(url, NoiseConfig.class);
    }
}
