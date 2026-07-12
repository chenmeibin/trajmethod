package com.urbancomputing.geomesa.apply.experiment.preprocess;

import com.urbancomputing.geomesa.apply.experiment.config.PreprocessConfig;
import com.urbancomputing.geomesa.apply.std.spark.NoiseFilterApp;
import com.urbancomputing.geomesa.apply.std.spark.SegmenterApp;
import com.urbancomputing.geomesa.model.basic.trajectory.Trajectory;
import org.apache.spark.api.java.JavaRDD;

public class Preprocess {
    private PreprocessConfig preprocessConfig;

    public Preprocess(PreprocessConfig preprocessConfig) {
        this.preprocessConfig = preprocessConfig;
    }

    public JavaRDD<Trajectory> process(JavaRDD<Trajectory> rawTraj) {
        //noise filter
        JavaRDD<Trajectory> noiseFilteredTraj = NoiseFilterApp.filter(rawTraj, preprocessConfig.getFilterParams());

        //segment
        return SegmenterApp.segment(noiseFilteredTraj, preprocessConfig.getSegmenterParams());
    }
}
