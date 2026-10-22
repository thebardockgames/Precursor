package org.json;

import java.util.*;

/** Minimal org.json subset used by the launcher: parse, typed getters, put, toString. (AI-assisted) */
public class JSONObject {
    private final Map<String, Object> map = new LinkedHashMap<>();

    public JSONObject() { }

    public JSONObject(String text) throws JSONException {
        Parser p = new Parser(text);
        Object v = p.value();
        p.ws();
        if (!(v instanceof JSONObject) || p.i != text.length()) throw new JSONException("not a JSON object");
        map.putAll(((JSONObject) v).map);
    }

    public JSONObject put(String key, Object value) throws JSONException {
        if (key == null) throw new JSONException("null key");
        map.put(key, value);
        return this;
    }
    public JSONObject put(String key, boolean value) throws JSONException { return put(key, (Object) value); }
    public JSONObject put(String key, int value) throws JSONException { return put(key, (Object) value); }
    public JSONObject put(String key, long value) throws JSONException { return put(key, (Object) value); }
    public Object remove(String key) { return map.remove(key); }
    public boolean has(String key) { return map.containsKey(key); }
    public String optString(String key) { return optString(key, ""); }
    public String optString(String key, String fallback) {
        Object v = map.get(key);
        return v == null ? fallback : String.valueOf(v);
    }
    public String getString(String key) throws JSONException {
        Object v = map.get(key);
        if (v == null) throw new JSONException("missing " + key);
        return String.valueOf(v);
    }
    public int optInt(String key, int fallback) {
        Object v = map.get(key);
        return v instanceof Number ? ((Number) v).intValue() : fallback;
    }
    public boolean optBoolean(String key, boolean fallback) {
        Object v = map.get(key);
        return v instanceof Boolean ? (Boolean) v : fallback;
    }
    public JSONObject optJSONObject(String key) {
        Object v = map.get(key);
        return v instanceof JSONObject ? (JSONObject) v : null;
    }
    public JSONArray optJSONArray(String key) {
        Object v = map.get(key);
        return v instanceof JSONArray ? (JSONArray) v : null;
    }
    public JSONArray getJSONArray(String key) throws JSONException {
        Object v = map.get(key);
        if (!(v instanceof JSONArray)) throw new JSONException("no array " + key);
        return (JSONArray) v;
    }

    static String quoteValue(Object v) {
        if (v == null) return "null";
        if (v instanceof Number || v instanceof Boolean) return String.valueOf(v);
        if (v instanceof JSONObject || v instanceof JSONArray) return v.toString();
        StringBuilder out = new StringBuilder("\"");
        for (char c : String.valueOf(v).toCharArray()) {
            if (c == '"' || c == '\\') out.append('\\').append(c);
            else if (c < 0x20) out.append(String.format("\\u%04x", (int) c));
            else out.append(c);
        }
        return out.append('"').toString();
    }

    @Override public String toString() {
        StringBuilder out = new StringBuilder("{");
        boolean first = true;
        for (Map.Entry<String, Object> e : map.entrySet()) {
            if (!first) out.append(',');
            first = false;
            out.append(quoteValue(e.getKey())).append(':').append(quoteValue(e.getValue()));
        }
        return out.append('}').toString();
    }

    private static final class Parser {
        final String s;
        int i;
        Parser(String s) { this.s = s; }
        void ws() { while (i < s.length() && Character.isWhitespace(s.charAt(i))) i++; }
        boolean peek(char c) { return i < s.length() && s.charAt(i) == c; }
        void expect(char c) throws JSONException {
            if (!peek(c)) throw new JSONException("expected " + c + " at " + i);
            i++;
        }
        Object value() throws JSONException {
            ws();
            if (i >= s.length()) throw new JSONException("eof");
            char c = s.charAt(i);
            if (c == '{') {
                i++;
                JSONObject o = new JSONObject();
                ws();
                if (peek('}')) { i++; return o; }
                while (true) {
                    ws();
                    String k = string();
                    ws();
                    expect(':');
                    o.map.put(k, value());
                    ws();
                    if (peek(',')) { i++; continue; }
                    expect('}');
                    return o;
                }
            }
            if (c == '[') {
                i++;
                JSONArray a = new JSONArray();
                ws();
                if (peek(']')) { i++; return a; }
                while (true) {
                    a.put(value());
                    ws();
                    if (peek(',')) { i++; continue; }
                    expect(']');
                    return a;
                }
            }
            if (c == '"') return string();
            if (s.startsWith("true", i)) { i += 4; return true; }
            if (s.startsWith("false", i)) { i += 5; return false; }
            if (s.startsWith("null", i)) { i += 4; return null; }
            int start = i;
            while (i < s.length() && "+-0123456789.eE".indexOf(s.charAt(i)) >= 0) i++;
            if (start == i) throw new JSONException("bad value at " + i);
            String n = s.substring(start, i);
            if (n.contains(".") || n.contains("e") || n.contains("E")) return Double.parseDouble(n);
            long l = Long.parseLong(n);
            return l == (int) l ? (Object) (int) l : (Object) l;
        }
        String string() throws JSONException {
            expect('"');
            StringBuilder out = new StringBuilder();
            while (i < s.length()) {
                char c = s.charAt(i++);
                if (c == '"') return out.toString();
                if (c != '\\') { out.append(c); continue; }
                char e = s.charAt(i++);
                switch (e) {
                    case 'n': out.append('\n'); break;
                    case 't': out.append('\t'); break;
                    case 'r': out.append('\r'); break;
                    case 'b': out.append('\b'); break;
                    case 'f': out.append('\f'); break;
                    case 'u': out.append((char) Integer.parseInt(s.substring(i, i + 4), 16)); i += 4; break;
                    default: out.append(e);
                }
            }
            throw new JSONException("unterminated string");
        }
    }
}
