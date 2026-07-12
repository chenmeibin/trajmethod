package com.urbancomputing.geomesa.apply.experiment;

import com.urbancomputing.geomesa.apply.experiment.config.PreprocessStoreConfig;
import com.urbancomputing.geomesa.apply.experiment.preprocess.Preprocess;
import com.urbancomputing.geomesa.apply.experiment.store.Store;
import com.urbancomputing.geomesa.apply.utils.FileSystemHelper;
import com.urbancomputing.geomesa.apply.utils.SparkSessionUtils;
import com.urbancomputing.geomesa.io.config.input.HDFSInputConfig;
import com.urbancomputing.geomesa.io.load.ILoader;
import com.urbancomputing.geomesa.model.basic.trajectory.Trajectory;
import org.apache.hadoop.hbase.exceptions.IllegalArgumentIOException;
import org.apache.log4j.Logger;
import org.apache.spark.api.java.JavaRDD;
import org.apache.spark.sql.SparkSession;

import java.io.IOException;

public class PreprocessStoreApp {
    /**
     * logger
     */
    private static final Logger logger = Logger.getLogger(PreprocessStoreApp.class);

    /**
     * @param args
     * @return : void
     **/
    public static void main(String[] args) throws IOException {
        String fileStr = FileSystemHelper.readFully(args[0], args[1]);
        PreprocessStoreConfig preprocessStoreConfig = PreprocessStoreConfig.parse(fileStr);

        logger.info("Init sparkSession...");
        boolean isLocal = false;

        if (!(preprocessStoreConfig.getInputConfig() instanceof HDFSInputConfig)) {
            logger.error("Invalid inputType");
            throw new IllegalArgumentIOException("Do not support other inputType except hdfs");
        }

        try (SparkSession sparkSession = SparkSessionUtils.createSession(preprocessStoreConfig.getInputConfig(), PreprocessStoreApp.class.getName(), isLocal)) {            logger.info("Start load");
            logger.info("Start load");
            ILoader trajLoader = ILoader.getLoader(preprocessStoreConfig.getInputConfig());
            JavaRDD<Trajectory> rawTraj = trajLoader.loadTrajectory(sparkSession, preprocessStoreConfig.getInputConfig(), preprocessStoreConfig.getDataConfig());
            logger.info("Complete load");

            logger.info("Start preprocess");
            Preprocess preprocess = new Preprocess(preprocessStoreConfig.getPreprocessConfig());
            JavaRDD<Trajectory> preprocessedTraj = preprocess.process(rawTraj);
            logger.info("Complete preprocess");

            logger.info("Start store");
            Store store = new Store(preprocessStoreConfig.getStoreConfig());
            store.store(preprocessedTraj, preprocessStoreConfig.getDataConfig());
            logger.info("Complete store");

        } catch (Exception e) {
            e.printStackTrace();
        }
    }


}
