package org.json;

import java.util.*;

/** Minimal org.json subset used by the launcher. (AI-assisted) */
public class JSONArray {
    final List<Object> values = new ArrayList<>();
    public int length() { return values.size(); }
    public JSONArray put(Object value) { values.add(value); return this; }
    public JSONObject getJSONObject(int i) throws JSONException {
        Object v = values.get(i);
        if (!(v instanceof JSONObject)) throw new JSONException("not an object at " + i);
        return (JSONObject) v;
    }
    @Override public String toString() {
        StringBuilder out = new StringBuilder("[");
        for (int i = 0; i < values.size(); i++) {
            if (i > 0) out.append(',');
            out.append(JSONObject.quoteValue(values.get(i)));
        }
        return out.append(']').toString();
    }
}
