package com.urbancomputing.geomesa.apply.experiment.store;

import com.urbancomputing.geomesa.apply.experiment.config.StoreConfig;
import com.urbancomputing.geomesa.io.config.output.GeoMesaOutputConfig;
import com.urbancomputing.geomesa.io.convert.geomesa.OutputSchemaEnum;
import com.urbancomputing.geomesa.io.store.IStore;
import com.urbancomputing.geomesa.model.basic.trajectory.Trajectory;
import com.urbancomputing.geomesa.model.conf.IDataConfig;
import org.apache.spark.api.java.JavaRDD;

import java.io.IOException;

public class Store {
    private StoreConfig storeConfig;

    public Store(StoreConfig storeConfig) {
        this.storeConfig = storeConfig;
    }

    public void store(JavaRDD<Trajectory> storeTraj, IDataConfig dataConfig) throws IOException {
        if (!(storeConfig.getOutputConfig() instanceof GeoMesaOutputConfig)) {
            throw new IllegalArgumentException("outputConfig type is not geoMesa");
        }
        GeoMesaOutputConfig geoMesaOutputConfig = (GeoMesaOutputConfig) storeConfig.getOutputConfig();
        String baseSft = geoMesaOutputConfig.getSftName();
        if (storeConfig.isStorePoint()) {
            geoMesaOutputConfig.setSftName(String.format("%s_v", baseSft));
            geoMesaOutputConfig.setSchemaType(OutputSchemaEnum.POINT_BASED_TRAJECTORY);
            IStore vStore = IStore.getStore(storeConfig.getOutputConfig(), dataConfig);
            vStore.storeTrajectory(storeTraj);
        }
        if(storeConfig.isStoreList()){
            geoMesaOutputConfig.setSftName(String.format("%s_h", baseSft));
            geoMesaOutputConfig.setSchemaType(OutputSchemaEnum.GPS_LIST_BASED_TRAJECTORY);
            IStore hStore = IStore.getStore(storeConfig.getOutputConfig(), dataConfig);
            hStore.storeTrajectory(storeTraj);
        }

    }
}
