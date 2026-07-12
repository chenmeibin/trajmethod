package com.urbancomputing.geomesa.apply.std.config;

import com.fasterxml.jackson.annotation.JsonCreator;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.core.JsonParseException;
import com.fasterxml.jackson.databind.JsonMappingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.datatype.jsr310.JavaTimeModule;
import com.urbancomputing.geomesa.process.preprocess.mapmatching.ivmm.IvmmParams;
import com.urbancomputing.geomesa.model.conf.IDataConfig;
import com.urbancomputing.geomesa.io.config.input.InputConfig;
import com.urbancomputing.geomesa.io.config.output.OutputConfig;

import java.io.IOException;
import java.io.Serializable;
import java.net.URL;

/**
 * @author : hehuajun3
 * @description : map matching 的配置
 * @date : Created in 2019-03-23 14:42
 * @modified by :
 **/
public class MapMatchingConfig implements Serializable {
    /**
     *
     */
    private final static ObjectMapper MAPPER = new ObjectMapper()
            .registerModule(new JavaTimeModule());
    /**
     * 地图匹配的方式
     */
    private MapMatchingType mapMatchingType;

    /**
     * 轨迹数据加载配置
     */
    private InputConfig trajInputConfig;
    /**
     * 数据映射
     */
    private IDataConfig dataConfig;
    /**
     * 路网数据加载配置
     */
    private InputConfig roadNetworkConfig;

    /**
     * 过滤参数
     */
    private IvmmParams ivmmParams;

    /**
     * 输出配置
     */
    private OutputConfig outputConfig;

    /**
     * @param mapMatchingType   匹配类型
     * @param trajInputConfig   轨迹输入配置
     * @param dataConfig        数据格式配置
     * @param roadNetworkConfig 路网配置
     * @param ivmmParams        路网匹配算法参数
     * @param outputConfig      输出配置
     * @description: MapMatchingConfig的构造函数
     */
    @JsonCreator
    public MapMatchingConfig(@JsonProperty("mapMatchingType") MapMatchingType mapMatchingType,
                             @JsonProperty("trajInputConfig") InputConfig trajInputConfig,
                             @JsonProperty("dataConfig") IDataConfig dataConfig,
                             @JsonProperty("roadNetworkConfig") InputConfig roadNetworkConfig,
                             @JsonProperty("ivmmParams") IvmmParams ivmmParams,
                             @JsonProperty("outputConfig") OutputConfig outputConfig) {
        this.mapMatchingType = mapMatchingType;
        this.trajInputConfig = trajInputConfig;
        this.dataConfig = dataConfig;
        this.roadNetworkConfig = roadNetworkConfig;
        this.ivmmParams = ivmmParams;
        this.outputConfig = outputConfig;
    }

    public MapMatchingType getMapMatchingType() {
        return mapMatchingType;
    }

    public InputConfig getTrajInputConfig() {
        return trajInputConfig;
    }

    public IDataConfig getDataConfig() {
        return dataConfig;
    }

    public InputConfig getRoadNetworkConfig() {
        return roadNetworkConfig;
    }

    public IvmmParams getIvmmParams() {
        return ivmmParams;
    }

    public OutputConfig getOutputConfig() {
        return outputConfig;
    }

    /**
     * 从字符串中解析出地图匹配的配置
     *
     * @param raw 原始字符串
     * @return : com.urbancomputing.geomesa.apply.std.config.MapMatchingConfig 地图匹配的配置
     **/
    public static MapMatchingConfig parse(String raw) throws JsonParseException {
        try {
            return MAPPER.readValue(raw, MapMatchingConfig.class);
        } catch (JsonMappingException e) {
            throw new IllegalArgumentException(e.toString());
        } catch (JsonParseException e) {
            throw e;
        } catch (IOException e) {
            throw new RuntimeException();
        }
    }

    /**
     * 从URL中解析出地图匹配的配置
     *
     * @param url url
     * @return : com.urbancomputing.geomesa.apply.std.config.MapMatchingConfig 地图匹配的配置
     **/
    public static MapMatchingConfig parse(URL url) throws IOException {
        return MAPPER.readValue(url, MapMatchingConfig.class);
    }

    public enum MapMatchingType {
        /**
         * matchTrajToMapMatchedTraj
         */
        MATCH_TRAJECTORY_TO_MM_POINT(Constants.MATCH_TRAJECTORY_TO_MM_POINT),
        /**
         * matchTrajToRoute
         */
        MATCH_TRAJECTORY_TO_ROUTE(Constants.MATCH_TRAJECTORY_TO_ROUTE),
        /**
         * match mmTrajToRoute
         */
        MATCH_MM_TRAJECTORY_TO_ROUTE(Constants.MATCH_MM_TRAJECTORY_TO_ROUTE);

        String type;

        /**
         * MapMatchingType的构造函数
         */
        MapMatchingType(String type) {
            this.type = type;
        }

        class Constants {
            /**
             * matchTrajToMapMatchedTraj
             */
            final static String MATCH_TRAJECTORY_TO_MM_POINT = "matchTrajToMapMatchedTraj";
            /**
             * matchTrajToRoute
             */
            final static String MATCH_TRAJECTORY_TO_ROUTE = "matchTrajToRoute";
            /**
             * match mmTrajToRoute
             */
            final static String MATCH_MM_TRAJECTORY_TO_ROUTE = "matchMMTrajToRoute";
        }

        public String getType() {
            return type;
        }
    }
}
