package com.urbancomputing.geomesa.apply.utils;

import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.FSDataInputStream;
import org.apache.hadoop.fs.FileStatus;
import org.apache.hadoop.fs.FileSystem;
import org.apache.hadoop.fs.Path;
import org.apache.log4j.Logger;

import java.io.IOException;
import java.net.URI;

/**
 * @description: hdfs文件读写
 * @author: He Huajun
 * @date : 2019-03-16 15:23
 **/
public class FileSystemHelper {
    /**
     * logger
     */
    private static final Logger logger = Logger.getLogger(FileSystemHelper.class);

    /**
     * 读取文件
     *
     * @param filePath 文件路径
     * @return 文件字符串
     */
    public static String readFully(String fsDefaultName, String filePath) {
        Configuration conf = new Configuration();
        conf.set("fs.defaultFS", fsDefaultName);
        Path path = new Path(filePath);
        try (FileSystem fs = FileSystem.get(URI.create(filePath), conf);
             FSDataInputStream in = fs.open(path)) {
            FileStatus stat = fs.getFileStatus(path);
            byte[] buffer = new byte[Integer.parseInt(String.valueOf(stat.getLen()))];
            in.readFully(0, buffer);
            return new String(buffer);
        } catch (IOException e) {
            logger.error(e.getMessage() + "/nFailed to read file from : " + filePath);
            e.printStackTrace();
            return null;
        }
    }
}
