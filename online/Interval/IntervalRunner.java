import java.io.BufferedWriter;
import java.io.FileWriter;
import java.io.IOException;

public class IntervalRunner {
    public static void main(String[] args) throws IOException {
        if (args.length != 3) {
            System.err.println("Usage: java IntervalRunner.java <input_path> <error> <output_path>");
            System.exit(1);
        }

        String inputPath = args[0];
        double error = Double.parseDouble(args[1]);
        String outputPath = args[2];

        Interval.error = error;
        Interval.R.clear();

        long start = System.nanoTime();
        Interval.one(inputPath);
        long end = System.nanoTime();

        try (BufferedWriter writer = new BufferedWriter(new FileWriter(outputPath))) {
            for (Object obj : Interval.R) {
                Interval.Point p = (Interval.Point) obj;
                writer.write(p.x + " " + p.y + " " + p.time);
                writer.newLine();
            }
            writer.write(String.format(java.util.Locale.US, "%.6f", (end - start) / 1_000_000_000.0));
            writer.newLine();
        }
    }
}
