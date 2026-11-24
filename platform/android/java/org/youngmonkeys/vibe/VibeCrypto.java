package org.youngmonkeys.vibe;

import android.net.http.X509TrustManagerExtensions;

import java.io.ByteArrayInputStream;
import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.security.KeyStore;
import java.security.PublicKey;
import java.security.Signature;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.util.Collection;
import java.util.List;
import java.util.Locale;
import javax.crypto.Cipher;
import javax.crypto.spec.IvParameterSpec;
import javax.crypto.spec.SecretKeySpec;
import javax.net.ssl.TrustManager;
import javax.net.ssl.TrustManagerFactory;
import javax.net.ssl.X509TrustManager;

final class VibeCrypto {

    private static final int RSA_BITS = 2048;

    private VibeCrypto() {}

    static KeyPair generate() throws Exception {
        KeyPairGenerator generator = KeyPairGenerator.getInstance("RSA");
        generator.initialize(RSA_BITS);
        return generator.generateKeyPair();
    }

    static byte[] publicKey(KeyPair keyPair) {
        return keyPair.getPublic().getEncoded();
    }

    static byte[] decrypt(KeyPair keyPair, byte[] data) throws Exception {
        Cipher cipher = Cipher.getInstance("RSA/ECB/PKCS1Padding");
        cipher.init(Cipher.DECRYPT_MODE, keyPair.getPrivate());
        return cipher.doFinal(data);
    }

    static byte[] aes(boolean encrypt, byte[] key, byte[] iv, byte[] data) throws Exception {
        Cipher cipher = Cipher.getInstance("AES/CBC/PKCS5Padding");
        cipher.init(
            encrypt ? Cipher.ENCRYPT_MODE : Cipher.DECRYPT_MODE,
            new SecretKeySpec(key, "AES"),
            new IvParameterSpec(iv)
        );
        return cipher.doFinal(data);
    }

    static String[] verify(byte[][] certificates, byte[] message, byte[] signature, String host) {
        X509Certificate[] chain = new X509Certificate[certificates.length];
        try {
            CertificateFactory factory = CertificateFactory.getInstance("X.509");
            for (int i = 0; i < certificates.length; ++i) {
                chain[i] = (X509Certificate) factory.generateCertificate(
                    new ByteArrayInputStream(certificates[i])
                );
            }
        } catch (Exception e) {
            return result("the server certificate does not parse", false, "");
        }
        if (chain.length == 0) {
            return result("the server certificate does not parse", false, "");
        }
        X509Certificate leaf = chain[0];
        String subject = commonName(leaf);
        PublicKey key = leaf.getPublicKey();
        String algorithm;
        String authType;
        if ("RSA".equals(key.getAlgorithm())) {
            algorithm = "SHA256withRSA";
            authType = "ECDHE_RSA";
        } else if ("EC".equals(key.getAlgorithm())) {
            algorithm = "SHA256withECDSA";
            authType = "ECDHE_ECDSA";
        } else {
            return result("the server key type is not RSA or EC", false, subject);
        }
        try {
            Signature verifier = Signature.getInstance(algorithm);
            verifier.initVerify(key);
            verifier.update(message);
            if (!verifier.verify(signature)) {
                return result("the handshake signature does not match", false, subject);
            }
        } catch (Exception e) {
            return result("the handshake signature does not match: " + e, false, subject);
        }
        return result(null, trusted(chain, authType, host), subject);
    }

    private static String[] result(String error, boolean trusted, String subject) {
        return new String[] {error, trusted ? "1" : "0", subject};
    }

    private static boolean trusted(X509Certificate[] chain, String authType, String host) {
        try {
            TrustManagerFactory factory = TrustManagerFactory.getInstance(
                TrustManagerFactory.getDefaultAlgorithm()
            );
            factory.init((KeyStore) null);
            for (TrustManager manager : factory.getTrustManagers()) {
                if (manager instanceof X509TrustManager) {
                    new X509TrustManagerExtensions((X509TrustManager) manager)
                        .checkServerTrusted(chain, authType, host);
                    return matchesHost(chain[0], host);
                }
            }
        } catch (Exception e) {
            return false;
        }
        return false;
    }

    private static boolean matchesHost(X509Certificate leaf, String host) throws Exception {
        String wanted = host.toLowerCase(Locale.ROOT);
        Collection<List<?>> names = leaf.getSubjectAlternativeNames();
        if (names == null) {
            return false;
        }
        for (List<?> entry : names) {
            if (entry.size() < 2 || !(entry.get(0) instanceof Integer) || !(entry.get(1) instanceof String)) {
                continue;
            }
            int type = (Integer) entry.get(0);
            String name = ((String) entry.get(1)).toLowerCase(Locale.ROOT);
            if (type == 7 && name.equals(wanted)) {
                return true;
            }
            if (type == 2 && dnsMatches(name, wanted)) {
                return true;
            }
        }
        return false;
    }

    private static boolean dnsMatches(String pattern, String host) {
        if (!pattern.startsWith("*.")) {
            return pattern.equals(host);
        }
        int dot = host.indexOf('.');
        return dot > 0 && host.substring(dot).equals(pattern.substring(1));
    }

    private static String commonName(X509Certificate certificate) {
        String name = certificate.getSubjectX500Principal().getName();
        int start = 0;
        boolean escaped = false;
        for (int i = 0; i <= name.length(); ++i) {
            boolean end = i == name.length();
            char c = end ? ',' : name.charAt(i);
            if (!end && escaped) {
                escaped = false;
                continue;
            }
            if (c == '\\') {
                escaped = true;
                continue;
            }
            if (c == ',' || c == '+') {
                String part = name.substring(start, i).trim();
                if (part.regionMatches(true, 0, "CN=", 0, 3)) {
                    return part.substring(3).replace("\\", "");
                }
                start = i + 1;
            }
        }
        return name;
    }
}
