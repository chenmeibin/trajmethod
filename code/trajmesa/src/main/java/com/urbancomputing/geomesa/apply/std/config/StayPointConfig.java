package com.urbancomputing.geomesa.apply.std.config;

import com.fasterxml.jackson.annotation.JsonCreator;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.core.JsonParseException;
import com.fasterxml.jackson.databind.JsonMappingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.urbancomputing.geomesa.model.conf.IDataConfig;
import com.urbancomputing.geomesa.process.preprocess.staypoint.StayPointDetectorEnum;
import com.urbancomputing.geomesa.io.config.input.InputConfig;
import com.urbancomputing.geomesa.io.config.output.OutputConfig;

import java.io.IOException;
import java.io.Serializable;
import java.net.URL;

/**
 * @author : hehuajun3
 * @description : 驻留点检测配置
 * @date : Created in 2019-03-19 12:19
 * @modified by :
 **/
public class StayPointConfig implements Serializable {
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
     * 最大驻留距离
     */
    private double maxStayDistInMeter;
    /**
     * 最大驻留时间
     */
    private double maxStayTimeInSecond;
    /**
     * 驻留类型
     */
    private StayPointDetectorEnum stayPointType;
    /**
     * 输出配置
     */
    private OutputConfig outputConfig;

    /**
     * StayPointConfig的构造函数
     */
    @JsonCreator
    public StayPointConfig(@JsonProperty("inputConfig") InputConfig inputConfig,
                           @JsonProperty("dataConfig") IDataConfig dataConfig,
                           @JsonProperty("maxStayDistInMeter") double maxStayDistInMeter,
                           @JsonProperty("maxStayTimeInSecond") double maxStayTimeInSecond,
                           @JsonProperty("stayPointType") StayPointDetectorEnum stayPointType,
                           @JsonProperty("outputConfig") OutputConfig outputConfig) {
        this.inputConfig = inputConfig;
        this.dataConfig = dataConfig;
        this.maxStayDistInMeter = maxStayDistInMeter;
        this.maxStayTimeInSecond = maxStayTimeInSecond;
        this.stayPointType = stayPointType;
        this.outputConfig = outputConfig;
    }

    /**
     * 从字符串中解析出分段配置
     *
     * @param raw 原始字符串
     * @return : com.urbancomputing.geomesa.apply.std.config.StayPointConfig 驻留点配置
     **/
    public static StayPointConfig parse(String raw) throws JsonParseException {
        try {
            return MAPPER.readValue(raw, StayPointConfig.class);
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
     * @return : com.urbancomputing.geomesa.apply.std.config.StayPointConfig 驻留点配置
     **/
    public static StayPointConfig parse(URL url) throws IOException {
        return MAPPER.readValue(url, StayPointConfig.class);
    }

    public InputConfig getInputConfig() {
        return inputConfig;
    }

    public IDataConfig getDataConfig() {
        return dataConfig;
    }

    public double getMaxStayDistInMeter() {
        return maxStayDistInMeter;
    }

    public double getMaxStayTimeInSecond() {
        return maxStayTimeInSecond;
    }

    public StayPointDetectorEnum getStayPointType() {
        return stayPointType;
    }

    public OutputConfig getOutputConfig() {
        return outputConfig;
    }
}
