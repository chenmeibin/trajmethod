package com.urbancomputing.geomesa.apply.std.spark;

import com.urbancomputing.geomesa.model.basic.trajectory.Trajectory;
import com.urbancomputing.geomesa.apply.std.config.DataLoaderConfig;
import com.urbancomputing.geomesa.io.load.ILoader;
import com.urbancomputing.geomesa.io.store.IStore;
import com.urbancomputing.geomesa.apply.utils.FileSystemHelper;
import com.urbancomputing.geomesa.apply.utils.SparkSessionUtils;
import org.apache.log4j.Logger;
import org.apache.spark.api.java.JavaRDD;
import org.apache.spark.sql.SparkSession;
import org.geotools.filter.text.cql2.CQLException;

import java.io.IOException;

public class DataLoaderAPP {
    /**
     * logger
     */
    private static final Logger logger = Logger.getLogger(DataLoaderAPP.class);

    public static void main(String[] args) throws IOException, CQLException {
        String fileStr = FileSystemHelper.readFully(args[0], args[1]);
        DataLoaderConfig dataLoaderConfig = DataLoaderConfig.parse(fileStr);
        logger.info("Init sparkSession...");
        boolean isLocal = false;
        int localIndex = 2;
        try {
            isLocal = Boolean.parseBoolean(args[localIndex]);
        } catch (Exception ignored) {
        }
        try (SparkSession sparkSession = SparkSessionUtils.createSession(dataLoaderConfig.getInputConfig(), SegmenterApp.class.getName(), isLocal)) {
            ILoader iLoader = ILoader.getLoader(dataLoaderConfig.getInputConfig());
            JavaRDD<Trajectory> trajRDD = iLoader.loadTrajectory(sparkSession, dataLoaderConfig.getInputConfig(), dataLoaderConfig.getDataConfig());
            IStore iStore = IStore.getStore(dataLoaderConfig.getOutputConfig(), dataLoaderConfig.getDataConfig());
            iStore.storeTrajectory(trajRDD);
            logger.info("Finished!");
        }
    }
}
