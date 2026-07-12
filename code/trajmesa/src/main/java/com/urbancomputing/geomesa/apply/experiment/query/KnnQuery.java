package com.urbancomputing.geomesa.apply.experiment.query;

import com.urbancomputing.geomesa.algorithm.similarity.Frechet;
import com.urbancomputing.geomesa.algorithm.similarity.Hausdorff;
import com.urbancomputing.geomesa.apply.experiment.utils.QueryTrajFetcher;
import com.urbancomputing.geomesa.io.load.query.knn.TrajectoryKNearestNeighbor;
import com.urbancomputing.geomesa.io.load.query.trajectory.BaseTrajectoryQuery;
import com.urbancomputing.geomesa.io.params.GeomesaInitParams;
import com.urbancomputing.geomesa.model.basic.point.GeodeticPoint;
import com.urbancomputing.geomesa.model.basic.trajectory.Trajectory;
import org.geotools.data.DataStore;
import org.geotools.data.DataStoreFinder;
import org.geotools.filter.text.cql2.CQLException;

import java.io.IOException;
import java.util.Arrays;
import java.util.List;


public class KnnQuery {
    public static int g = 15;
    public static boolean isFilterAble = true;
    public static boolean isLengthAble = false;

    public static void main(String[] args) throws IOException, CQLException {
        String hbaseZookeepers = "host01:2181,host02:2181,host03:2181";
        String hbaseCatalog = "tdrive_catalog";
        String hSftName = "tdrive_h";
        List<String> trajIdList = Arrays.asList("100_1201949099_1202147792","1002_1202278794_1202393682");
        GeodeticPoint point = new GeodeticPoint(116.27077, 39.92875);
        int k = 10;

        //create DataStore
        GeomesaInitParams params = new GeomesaInitParams(hbaseZookeepers, hbaseCatalog, hSftName);
        DataStore dataStore = DataStoreFinder.getDataStore(params.getParams());

        //create TrajectoryKNearestNeighbor
        BaseTrajectoryQuery trajectoryQuery = BaseTrajectoryQuery.getTrajectoryQuery(dataStore, hSftName);
        TrajectoryKNearestNeighbor trajectoryKNearestNeighbor = new TrajectoryKNearestNeighbor(trajectoryQuery, g, isFilterAble);

        //point knn query
        List<Trajectory> pointKnnQueryResult = trajectoryKNearestNeighbor.searchKNearestNeighbor(point, k);

        //fetch query trajectory by trajId
        List<Trajectory> queryTrajList = QueryTrajFetcher.fetchQeuryTraj(dataStore, hSftName, trajIdList);

        //trajectory knn query
        List<Trajectory> trajKnnQueryResultByFrechet = trajectoryKNearestNeighbor.searchKNearestNeighbor(queryTrajList.get(0), k, new Frechet(), isLengthAble);
        List<Trajectory> trajKnnQueryResultByHausdorff = trajectoryKNearestNeighbor.searchKNearestNeighbor(queryTrajList.get(0), k, new Hausdorff(), isLengthAble);
    }
}
