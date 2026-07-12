package com.urbancomputing.geomesa.apply.std.spark;

import com.fasterxml.jackson.core.JsonParseException;
import com.urbancomputing.geomesa.model.basic.trajectory.Trajectory;
import com.urbancomputing.geomesa.process.preprocess.noisefiltering.INoiseFilter;
import com.urbancomputing.geomesa.process.preprocess.noisefiltering.params.BaseFilterParams;
import com.urbancomputing.geomesa.apply.std.config.NoiseConfig;
import com.urbancomputing.geomesa.io.load.ILoader;
import com.urbancomputing.geomesa.io.store.IStore;
import com.urbancomputing.geomesa.apply.utils.FileSystemHelper;
import com.urbancomputing.geomesa.apply.utils.SparkSessionUtils;
import org.apache.log4j.Logger;
import org.apache.spark.api.java.JavaRDD;
import org.apache.spark.sql.SparkSession;
import org.geotools.filter.text.cql2.CQLException;

import java.io.IOException;
import java.util.List;
import java.util.Objects;

public class NoiseFilterApp {
    /**
     * logger
     */
    private static final Logger logger = Logger.getLogger(NoiseFilterApp.class);

    /**
     * 过滤
     *
     * @param rawTrajectory 原始轨迹rdd
     * @param params        过滤参数
     * @return JavaRDD<Trajectory>
     */
    public static JavaRDD<Trajectory> filter(JavaRDD<Trajectory> rawTrajectory, BaseFilterParams params) {
        INoiseFilter filter = INoiseFilter.getFilter(params);
        return rawTrajectory.map(filter::filter).flatMap(List::iterator).filter(Objects::nonNull).map(o -> (Trajectory) o);
    }

    /**
     * @param args 参数
     * @throws JsonParseException
     */
    public static void main(String[] args) throws IOException, CQLException {
        String fileStr = FileSystemHelper.readFully(args[0], args[1]);
        NoiseConfig noiseConfig = NoiseConfig.parse(fileStr);
        logger.info("Init sparkSession...");
        boolean isLocal = false;
        int localIndex = 2;
        try {
            isLocal = Boolean.parseBoolean(args[localIndex]);
        } catch (Exception ignored) {
        }
        try (SparkSession sparkSession = SparkSessionUtils.createSession(noiseConfig.getInputConfig(), NoiseFilterApp.class.getName(), isLocal)) {
            ILoader iLoader = ILoader.getLoader(noiseConfig.getInputConfig());
            JavaRDD<Trajectory> trajRDD = iLoader.loadTrajectory(sparkSession, noiseConfig.getInputConfig(), noiseConfig.getDataConfig());
            logger.info("Completion of loading trajectories");
            logger.info("Start Filtering...");
            JavaRDD<Trajectory> resultTraj = NoiseFilterApp.filter(trajRDD, noiseConfig.getFilterParams());
            logger.info("Storing...");
            IStore iStore = IStore.getStore(noiseConfig.getOutputConfig(), noiseConfig.getDataConfig());
            iStore.storeTrajectory(resultTraj);
            logger.info("Finished!");
        }
    }
}
