package org.youngmonkeys.vibe;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.Arrays;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

final class VibeHttp {

    private static final ExecutorService EXECUTOR = Executors.newCachedThreadPool();

    private VibeHttp() {}

    static void send(
        final long request,
        final String method,
        final String url,
        final String[] headers,
        final byte[] body,
        final int timeoutMs
    ) {
        EXECUTOR.execute(new Runnable() {
            @Override
            public void run() {
                exchange(request, method, url, headers, body, timeoutMs);
            }
        });
    }

    private static void exchange(
        long request,
        String method,
        String url,
        String[] headers,
        byte[] body,
        int timeoutMs
    ) {
        int status = 0;
        byte[] answer = null;
        HttpURLConnection connection = null;
        try {
            connection = (HttpURLConnection) new URL(url).openConnection();
            connection.setRequestMethod(method);
            connection.setConnectTimeout(timeoutMs);
            connection.setReadTimeout(timeoutMs);
            connection.setUseCaches(false);
            for (int i = 0; i + 1 < headers.length; i += 2) {
                connection.setRequestProperty(headers[i], headers[i + 1]);
            }
            if (body != null && body.length > 0) {
                connection.setDoOutput(true);
                connection.setFixedLengthStreamingMode(body.length);
                try (OutputStream out = connection.getOutputStream()) {
                    out.write(body);
                }
            }
            status = connection.getResponseCode();
            InputStream in = status >= 400
                ? connection.getErrorStream()
                : connection.getInputStream();
            if (in != null) {
                try (InputStream stream = in) {
                    answer = readAll(stream);
                }
            }
        } catch (Exception e) {
            status = 0;
            answer = null;
        } finally {
            if (connection != null) {
                connection.disconnect();
            }
            if (body != null) {
                Arrays.fill(body, (byte) 0);
            }
        }
        nativeDone(request, status, answer);
        if (answer != null) {
            Arrays.fill(answer, (byte) 0);
        }
    }

    private static byte[] readAll(InputStream in) throws Exception {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buffer = new byte[8192];
        int read;
        while ((read = in.read(buffer)) > 0) {
            out.write(buffer, 0, read);
        }
        Arrays.fill(buffer, (byte) 0);
        return out.toByteArray();
    }

    private static native void nativeDone(long request, int status, byte[] body);
}
