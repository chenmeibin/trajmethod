package com.urbancomputing.geomesa.apply.std.spark;

import com.urbancomputing.geomesa.model.basic.trajectory.Trajectory;
import com.urbancomputing.geomesa.process.preprocess.staypoint.IStayPointDetector;
import com.urbancomputing.geomesa.model.basic.box.StayPoint;
import com.urbancomputing.geomesa.process.preprocess.staypoint.StayPointDetectorEnum;
import com.urbancomputing.geomesa.apply.std.config.StayPointConfig;
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

public class StayPointApp {
    /**
     * logger
     */
    private static final Logger logger = Logger.getLogger(StayPointApp.class);

    /**
     * @param rawTrajectory       原始轨迹RDD
     * @param maxStayDistInMeter  最大驻留距离
     * @param maxStayTimeInSecond 最大驻留时间
     * @param stayPointType       驻留类型
     * @return JavaRDD<StayPoint>
     */
    public static JavaRDD<StayPoint> detect(JavaRDD<Trajectory> rawTrajectory, double maxStayDistInMeter, double maxStayTimeInSecond, StayPointDetectorEnum stayPointType) {
        IStayPointDetector stayPointDetector = IStayPointDetector.getStayPointDetector(maxStayDistInMeter, maxStayTimeInSecond, stayPointType);
        return rawTrajectory.map(stayPointDetector::detect).flatMap(List::iterator);
    }

    /**
     * @param args 参数
     * @throws IOException
     */
    public static void main(String[] args) throws IOException, CQLException {
        String fileStr = FileSystemHelper.readFully(args[0], args[1]);
        StayPointConfig stayPointConfig = StayPointConfig.parse(fileStr);
        logger.info("Init sparkSession...");
        boolean isLocal = false;
        int localIndex = 2;
        try {
            isLocal = Boolean.parseBoolean(args[localIndex]);
        } catch (Exception ignored) {
        }
        try (SparkSession sparkSession = SparkSessionUtils.createSession(stayPointConfig.getInputConfig(), StayPointApp.class.getName(), isLocal)) {
            ILoader iLoader = ILoader.getLoader(stayPointConfig.getInputConfig());
            JavaRDD<Trajectory> trajRDD = iLoader.loadTrajectory(sparkSession, stayPointConfig.getInputConfig(), stayPointConfig.getDataConfig());
            logger.info("Completion of loading trajectories");
            logger.info("Start detecting StayPoint...");
            JavaRDD<StayPoint> resultSP = StayPointApp.detect(trajRDD, stayPointConfig.getMaxStayDistInMeter(), stayPointConfig.getMaxStayTimeInSecond(), stayPointConfig.getStayPointType());
            logger.info("Storing...");
            IStore iStore = IStore.getStore(stayPointConfig.getOutputConfig(), stayPointConfig.getDataConfig());
            iStore.storeStayPoint(resultSP);
            logger.info("Finished!");
        }
    }
}
