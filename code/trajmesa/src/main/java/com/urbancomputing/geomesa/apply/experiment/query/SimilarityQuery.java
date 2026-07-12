package com.urbancomputing.geomesa.apply.experiment.query;

import com.urbancomputing.geomesa.algorithm.similarity.Frechet;
import com.urbancomputing.geomesa.algorithm.similarity.Hausdorff;
import com.urbancomputing.geomesa.apply.experiment.utils.QueryTrajFetcher;
import com.urbancomputing.geomesa.io.load.query.trajectory.ListBasedTrajectoryQuery;
import com.urbancomputing.geomesa.io.params.GeomesaInitParams;
import com.urbancomputing.geomesa.model.basic.trajectory.Trajectory;
import org.geotools.data.DataStore;
import org.geotools.data.DataStoreFinder;
import org.geotools.filter.text.cql2.CQLException;

import java.io.IOException;
import java.util.Arrays;
import java.util.List;

public class SimilarityQuery {
    public static boolean isFilterAble = true;

    public static void main(String[] args) throws IOException, CQLException {
        String hbaseZookeepers = "host01:2181,host02:2181,host03:2181";
        String hbaseCatalog = "tdrive_catalog";
        String hSftName = "tdrive_h";
        List<String> trajIdList = Arrays.asList("100_1201949099_1202147792","1002_1202278794_1202393682");
        double threshold = 1500.0;

        //create DataStore
        GeomesaInitParams params = new GeomesaInitParams(hbaseZookeepers, hbaseCatalog, hSftName);
        DataStore dataStore = DataStoreFinder.getDataStore(params.getParams());

        //create ListBasedTrajectoryQuery
        ListBasedTrajectoryQuery hQuery = new ListBasedTrajectoryQuery(dataStore, hSftName);

        //fetch query trajectory by trajId
        List<Trajectory> queryTrajList = QueryTrajFetcher.fetchQeuryTraj(dataStore, hSftName, trajIdList);

        //similarity query
        List<Trajectory> queryResultByFrechet = hQuery.querySimilarTrajectories(new Frechet(), queryTrajList.get(0), threshold, isFilterAble);
        List<Trajectory> queryResultByHausdorff = hQuery.querySimilarTrajectories(new Hausdorff(), queryTrajList.get(0), threshold, isFilterAble);
    }
}



