package com.urbancomputing.geomesa.apply.experiment.utils;

import com.urbancomputing.geomesa.io.params.GeomesaInitParams;
import com.urbancomputing.geomesa.model.basic.trajectory.Trajectory;
import org.geotools.data.*;
import org.geotools.filter.text.cql2.CQLException;
import org.geotools.filter.text.ecql.ECQL;
import org.opengis.feature.simple.SimpleFeature;
import org.opengis.feature.simple.SimpleFeatureType;

import java.io.IOException;
import java.util.HashSet;
import java.util.Set;


public class FetchObjectId {

    public static void main(String[] args) throws IOException, CQLException {
        String hbaseZookeepers = "host01:2181,host02:2181,host03:2181";
        String hbaseCatalog = "tdrive_catalog";
        String sftName = "tdrive_h";
        int objectNum = 10;

        //create DataStore
        GeomesaInitParams params = new GeomesaInitParams(hbaseZookeepers, hbaseCatalog, sftName);
        DataStore dataStore = DataStoreFinder.getDataStore(params.getParams());

        //fetch distinct objectId
        Query query = new Query(params.getSFTName(), ECQL.toFilter("1=1"), new String[]{Trajectory.Schema.OBJECT_ID});
        Set<String> objectIdSet = new HashSet<>(objectNum);
        try (FeatureReader<SimpleFeatureType, SimpleFeature> reader = dataStore.getFeatureReader(query, Transaction.AUTO_COMMIT)) {
            while (reader.hasNext()) {
                objectIdSet.add(reader.next().getAttribute(Trajectory.Schema.OBJECT_ID).toString());
                if (objectIdSet.size() == objectNum) {
                    break;
                }
            }
        }
        System.out.println(objectIdSet);
    }

}



