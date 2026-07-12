package com.urbancomputing.geomesa.apply.experiment.utils;

import com.urbancomputing.geomesa.io.params.GeomesaInitParams;
import com.urbancomputing.geomesa.model.basic.trajectory.Trajectory;
import org.geotools.data.*;
import org.geotools.filter.text.cql2.CQLException;
import org.geotools.filter.text.ecql.ECQL;
import org.opengis.feature.simple.SimpleFeature;
import org.opengis.feature.simple.SimpleFeatureType;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

public class FetchTrajId {
    /**
     * 查询handle
     */
    private final static String HANDLE = "query_traj_id";

    public static void main(String[] args) throws IOException, CQLException {
        String hbaseZookeepers = "host01:2181,host02:2181,host03:2181";
        String hbaseCatalog = "tdrive_catalog";
        String sftName = "tdrive_h";
        int trajNum = 10;

        //create DataStore
        GeomesaInitParams params = new GeomesaInitParams(hbaseZookeepers, hbaseCatalog, sftName);
        DataStore dataStore = DataStoreFinder.getDataStore(params.getParams());

        //fetch distinct trajId
        Query query = new Query(params.getSFTName(), ECQL.toFilter("1=1"), trajNum, new String[]{Trajectory.Schema.TRAJECTORY_ID}, HANDLE);
        List<String> trajIdList = new ArrayList<>(trajNum);
        try (FeatureReader<SimpleFeatureType, SimpleFeature> reader = dataStore.getFeatureReader(query, Transaction.AUTO_COMMIT)) {
            while (reader.hasNext()) {
                trajIdList.add(reader.next().getAttribute(Trajectory.Schema.TRAJECTORY_ID).toString());
            }
        }
        System.out.println(trajIdList);
    }

}



