package com.urbancomputing.geomesa.apply.experiment.utils;

import com.urbancomputing.geomesa.io.load.query.trajectory.ListBasedTrajectoryQuery;
import com.urbancomputing.geomesa.model.basic.trajectory.Trajectory;
import org.geotools.data.DataStore;
import org.geotools.filter.text.cql2.CQLException;

import java.io.IOException;
import java.util.List;

public class QueryTrajFetcher {
    public static List<Trajectory> fetchQeuryTraj(DataStore dataStore, String sftName, List<String> trajIdList) throws IOException, CQLException {
        StringBuilder builder = new StringBuilder();
        builder.append("(");
        for (String trajId : trajIdList) {
            builder.append("'").append(trajId).append("',");
        }
        builder.replace(builder.length() - 1, builder.length(), ")");
        String eCQL = Trajectory.Schema.TRAJECTORY_ID + " IN " + builder.toString();
        return new ListBasedTrajectoryQuery(dataStore, sftName).queryByECQL(eCQL);
    }
}
