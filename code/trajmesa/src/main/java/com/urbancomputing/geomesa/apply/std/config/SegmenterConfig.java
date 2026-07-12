package com.urbancomputing.geomesa.apply.std.config;

import com.fasterxml.jackson.annotation.JsonCreator;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.core.JsonParseException;
import com.fasterxml.jackson.databind.JsonMappingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.urbancomputing.geomesa.model.conf.IDataConfig;
import com.urbancomputing.geomesa.process.preprocess.segmentation.SegmenterParams;
import com.urbancomputing.geomesa.io.config.input.InputConfig;
import com.urbancomputing.geomesa.io.config.output.OutputConfig;

import java.io.IOException;
import java.io.Serializable;
import java.net.URL;

/**
 * @description: spark的配置文件
 * @author: He Huajun
 * @date : 2019-03-16 11:54
 **/
public class SegmenterConfig implements Serializable {
    /**
     * ObjectMapper
     */
    private final static ObjectMapper MAPPER = new ObjectMapper();

    /**
     * 数据加载配置
     */
    private InputConfig inputConfig;
    /**
     * 数据映射
     */
    private IDataConfig dataConfig;
    /**
     * 分段参数
     */
    private SegmenterParams segmenterParams;
    /**
     * 输出配置
     */
    private OutputConfig outputConfig;

    /**
     * SegmenterConfig的构造函数
     */
    @JsonCreator
    public SegmenterConfig(@JsonProperty("inputConfig") InputConfig inputConfig,
                           @JsonProperty("dataConfig") IDataConfig dataConfig,
                           @JsonProperty("segmenterParams") SegmenterParams segmenterParams,
                           @JsonProperty("outputConfig") OutputConfig outputConfig) {
        this.inputConfig = inputConfig;
        this.dataConfig = dataConfig;
        this.segmenterParams = segmenterParams;
        this.outputConfig = outputConfig;
    }

    public InputConfig getInputConfig() {
        return inputConfig;
    }

    public IDataConfig getDataConfig() {
        return dataConfig;
    }

    public SegmenterParams getSegmenterParams() {
        return segmenterParams;
    }

    public OutputConfig getOutputConfig() {
        return outputConfig;
    }

    /**
     * 从字符串中解析出分段配置
     *
     * @param raw 原始字符串
     * @return : com.urbancomputing.geomesa.apply.std.config.SegmenterConfig 分段配置
     **/
    public static SegmenterConfig parse(String raw) throws JsonParseException {
        try {
            return MAPPER.readValue(raw, SegmenterConfig.class);
        } catch (JsonMappingException e) {
            throw new IllegalArgumentException(e.toString());
        } catch (JsonParseException e) {
            throw e;
        } catch (IOException e) {
            throw new RuntimeException();
        }
    }

    /**
     * 从URL中解析出字符串
     *
     * @param url url
     * @return : com.urbancomputing.geomesa.apply.std.config.SegmenterConfig 分段配置
     **/
    public static SegmenterConfig parse(URL url) throws IOException {
        return MAPPER.readValue(url, SegmenterConfig.class);
    }
}
