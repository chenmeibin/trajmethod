package com.urbancomputing.geomesa.apply.std.spark;

import com.urbancomputing.geomesa.model.basic.road.RoadNetwork;
import com.urbancomputing.geomesa.model.basic.trajectory.MapMatchedTrajectory;
import com.urbancomputing.geomesa.model.basic.trajectory.RouteOfTrajectory;
import com.urbancomputing.geomesa.model.basic.trajectory.Trajectory;
import com.urbancomputing.geomesa.process.preprocess.mapmatching.ivmm.IvmmMapMatcher;
import com.urbancomputing.geomesa.process.preprocess.mapmatching.ivmm.IvmmParams;
import com.urbancomputing.geomesa.apply.std.config.MapMatchingConfig;
import com.urbancomputing.geomesa.io.load.ILoader;
import com.urbancomputing.geomesa.io.store.IStore;
import com.urbancomputing.geomesa.apply.utils.FileSystemHelper;
import com.urbancomputing.geomesa.apply.utils.SparkSessionUtils;
import org.apache.log4j.Logger;
import org.apache.spark.api.java.JavaRDD;
import org.apache.spark.broadcast.Broadcast;
import org.apache.spark.sql.SparkSession;
import org.geotools.filter.text.cql2.CQLException;
import scala.reflect.ClassManifestFactory;

import java.lang.UnsupportedOperationException;

import java.io.IOException;
import java.util.List;
import java.util.Objects;

public class MapMatchingApp {
    /**
     * logger
     */
    private static final Logger logger = Logger.getLogger(MapMatchingApp.class);
    /**
     * ivmm参数
     */
    private IvmmParams ivmmParams;

    /**
     *
     */
    public MapMatchingApp() {
        this.ivmmParams = new IvmmParams();
    }

    /**
     * 构造函数
     *
     * @param ivmmParams ivmmParams
     */
    public MapMatchingApp(IvmmParams ivmmParams) {
        this.ivmmParams = ivmmParams;
    }

    /**
     * @param args
     */
    public static void main(String[] args) throws IOException, CQLException {
        String fileStr = FileSystemHelper.readFully(args[0], args[1]);
        MapMatchingConfig mapMatchingConfig = MapMatchingConfig.parse(fileStr);
        //初始化sparkSession
        logger.info("Init sparkSession...");
        boolean isLocal = false;
        int localIndex = 2;
        try {
            isLocal = Boolean.parseBoolean(args[localIndex]);
        } catch (Exception ignored) {
        }
        try (SparkSession sparkSession = SparkSessionUtils.createSession(mapMatchingConfig.getTrajInputConfig(), MapMatchingApp.class.getName(), isLocal)) {
            logger.info("Loading roadNetwork...");
            ILoader loader = ILoader.getLoader(mapMatchingConfig.getRoadNetworkConfig());
            RoadNetwork roadNetwork = loader.loadRoadNetwork(sparkSession, mapMatchingConfig.getRoadNetworkConfig(), mapMatchingConfig.getDataConfig());
            logger.info("Completion of loading roadNetwork");
            logger.info("Loading trajectories...");
            ILoader trajectoryLoader = ILoader.getLoader(mapMatchingConfig.getTrajInputConfig());
            JavaRDD<Trajectory> trajectoryRDD = trajectoryLoader.loadTrajectory(sparkSession, mapMatchingConfig.getTrajInputConfig(), mapMatchingConfig.getDataConfig());
            logger.info("Completion of loading trajectories");
            MapMatchingApp mapMatchingAPP = new MapMatchingApp();
            IStore iStore = IStore.getStore(mapMatchingConfig.getOutputConfig(), null);
            switch (mapMatchingConfig.getMapMatchingType()) {
                case MATCH_TRAJECTORY_TO_MM_POINT:
                    logger.info("Starting MATCH_TRAJECTORY_TO_MM_POINT");
                    JavaRDD<MapMatchedTrajectory> mmTrajRDD = mapMatchingAPP.trajPointsToMMPoints(trajectoryRDD, roadNetwork);
                    logger.info("Completion of MATCH_TRAJECTORY_TO_MM_POINT");
                    logger.info("Storing...");
                    iStore.storeMMTrajectory(mmTrajRDD);
                    logger.info("Completion of storing MMTrajectory");
                    break;
                case MATCH_TRAJECTORY_TO_ROUTE:
                    logger.info("Starting MATCH_TRAJECTORY_TO_ROUTE");
                    JavaRDD<RouteOfTrajectory> routeOfTrajectoryRDD = mapMatchingAPP.matchTrajToRoute(trajectoryRDD, roadNetwork);
                    logger.info("Completion of MATCH_TRAJECTORY_TO_MM_POINT");
                    logger.info("Storing...");
                    iStore.storeRouteOfTrajectory(routeOfTrajectoryRDD);
                    logger.info("Completion of storing RouteOfTrajectory");
                    break;
                case MATCH_MM_TRAJECTORY_TO_ROUTE:
                    logger.error("The function of MATCH_MM_TRAJECTORY_TO_ROUTE is not implemented!");
                    throw new UnsupportedOperationException();
                default:
                    logger.error("NoSuchMethodError!");
                    throw new NoSuchMethodError();
            }
        }
    }

    /**
     * 轨迹点转换为地图匹配点
     *
     * @param trajRDD 轨迹rdd
     * @param rn      路网
     * @return JavaRDD<MapMatchedTrajectory>
     */
    public JavaRDD<MapMatchedTrajectory> trajPointsToMMPoints(JavaRDD<Trajectory> trajRDD, RoadNetwork rn) {
        IvmmMapMatcher mapMatching = new IvmmMapMatcher(rn, this.ivmmParams);
        final Broadcast<IvmmMapMatcher> ivmmMapMatcherBroadcast = trajRDD.context().broadcast(mapMatching, ClassManifestFactory.classType(IvmmMapMatcher.class));
        return trajRDD
                .map(t -> ivmmMapMatcherBroadcast.getValue().matchTrajToMapMatchedTraj(t)).filter(Objects::nonNull);
    }

    /**
     * 轨迹点转换为轨迹路径
     *
     * @param trajRDD 轨迹rdd
     * @param rn      路网
     * @return JavaRDD<RouteOfTrajectory>
     */
    public JavaRDD<RouteOfTrajectory> matchMMTrajToRoute(JavaRDD<MapMatchedTrajectory> trajRDD, RoadNetwork rn) {
        IvmmMapMatcher mapMatching = new IvmmMapMatcher(rn, this.ivmmParams);
        final Broadcast<IvmmMapMatcher> ivmmMapMatcherBroadcast = trajRDD.context().broadcast(mapMatching, ClassManifestFactory.classType(IvmmMapMatcher.class));
        return trajRDD
                .map(t -> ivmmMapMatcherBroadcast.getValue().matchTrajToRoute(t))
                //这里不能用Object::notNull，感觉很神奇，一换就报错!!!
                .filter(v -> null != v)
                .flatMap(List::iterator);
    }

    /**
     * 匹配轨迹到轨迹路径
     *
     * @param trajRDD 轨迹rdd
     * @param rn      路网
     * @return JavaRDD<RouteOfTrajectory>
     */
    public JavaRDD<RouteOfTrajectory> matchTrajToRoute(JavaRDD<Trajectory> trajRDD, RoadNetwork rn) {
        IvmmMapMatcher mapMatching = new IvmmMapMatcher(rn, this.ivmmParams);
        final Broadcast<IvmmMapMatcher> ivmmMapMatcherBroadcast = trajRDD.context().broadcast(mapMatching, ClassManifestFactory.classType(IvmmMapMatcher.class));
        return trajRDD
                .map(t -> ivmmMapMatcherBroadcast.getValue().matchTrajToRoute(t))
                //这里不能用Object::notNull，感觉很神奇，一换就报错!!!
                .filter(v -> null != v)
                .flatMap(List::iterator);
    }
}
