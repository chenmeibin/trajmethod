package com.urbancomputing.geomesa.apply.std.spark;

import com.fasterxml.jackson.core.JsonParseException;
import com.urbancomputing.geomesa.model.basic.trajectory.Trajectory;
import com.urbancomputing.geomesa.process.preprocess.segmentation.ISegmenter;
import com.urbancomputing.geomesa.process.preprocess.segmentation.SegmenterParams;
import com.urbancomputing.geomesa.apply.std.config.SegmenterConfig;
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

public class SegmenterApp {

    /**
     * logger
     */
    private static final Logger logger = Logger.getLogger(SegmenterApp.class);

    /**
     * 分段
     *
     * @param rawTrajectory 原始轨迹RDD
     * @param params        分段参数
     * @return JavaRDD<Trajectory>
     */
    public static JavaRDD<Trajectory> segment(JavaRDD<Trajectory> rawTrajectory, SegmenterParams params) {
        ISegmenter segmenter = ISegmenter.getSegmenter(params);
        return rawTrajectory.map(segmenter::segment).flatMap(List::iterator).filter(Objects::nonNull).map(o -> (Trajectory) o);
    }

    /**
     * @param args 参数
     * @throws JsonParseException 异常
     */
    public static void main(String[] args) throws IOException, CQLException {
        String fileStr = FileSystemHelper.readFully(args[0], args[1]);
        SegmenterConfig segmenterConfig = SegmenterConfig.parse(fileStr);
        logger.info("Init sparkSession...");
        boolean isLocal = false;
        int localIndex = 2;
        try {
            isLocal = Boolean.parseBoolean(args[localIndex]);
        } catch (Exception ignored) {
        }
        try (SparkSession sparkSession = SparkSessionUtils.createSession(segmenterConfig.getInputConfig(), SegmenterApp.class.getName(), isLocal)) {
            ILoader iLoader = ILoader.getLoader(segmenterConfig.getInputConfig());
            JavaRDD<Trajectory> trajRDD = iLoader.loadTrajectory(sparkSession, segmenterConfig.getInputConfig(), segmenterConfig.getDataConfig());
            logger.info("Completion of loading trajectories");
            logger.info("Start Segmentation...");
            SegmenterParams segmenterParams = segmenterConfig.getSegmenterParams();
            JavaRDD<Trajectory> resultTraj = SegmenterApp.segment(trajRDD, segmenterParams);
            logger.info("the size of resultTraj..." + resultTraj.count());
            logger.info("Storing...");
            IStore iStore = IStore.getStore(segmenterConfig.getOutputConfig(), segmenterConfig.getDataConfig());
            iStore.storeTrajectory(resultTraj);
            logger.info("Finished!");
        }
    }
}
