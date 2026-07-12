package com.urbancomputing.geomesa.apply.experiment.query;

import com.urbancomputing.geomesa.io.load.query.trajectory.ListBasedTrajectoryQuery;
import com.urbancomputing.geomesa.io.params.GeomesaInitParams;
import com.urbancomputing.geomesa.model.basic.box.MinimumBoundingBox;
import com.urbancomputing.geomesa.model.basic.trajectory.Trajectory;
import com.urbancomputing.geomesa.model.constant.DateDefaults;
import org.geotools.data.DataStore;
import org.geotools.data.DataStoreFinder;
import org.geotools.filter.text.cql2.CQLException;

import java.io.IOException;
import java.time.ZonedDateTime;
import java.util.List;

public class SpatialTemporalQuery {
    public static void main(String[] args) throws IOException, CQLException {
        String hbaseZookeepers = "host01:2181,host02:2181,host03:2181";
        String hbaseCatalog = "tdrive_catalog";
        String hSftName = "tdrive_h";
        ZonedDateTime startTime = ZonedDateTime.parse("2008-02-03 13:00:00", DateDefaults.DEFAULT_TIME_FORMATTER);
        ZonedDateTime endTime = ZonedDateTime.parse("2008-02-03 19:00:00", DateDefaults.DEFAULT_TIME_FORMATTER);
        MinimumBoundingBox spatialWindow = new MinimumBoundingBox(116.34000, 39.94000, 116.367, 39.967);

        //create dataStore
        GeomesaInitParams params = new GeomesaInitParams(hbaseZookeepers, hbaseCatalog, hSftName);
        DataStore dataStore = DataStoreFinder.getDataStore(params.getParams());

        //spatial temporal query
        ListBasedTrajectoryQuery hQuery = new ListBasedTrajectoryQuery(dataStore, hSftName);
        List<Trajectory> queryResult = hQuery.queryBySpatialBBoxTemporalRange(spatialWindow, startTime, endTime);
    }
}
