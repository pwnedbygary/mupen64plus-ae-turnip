import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.AssetManager;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;
import paulscode.android.mupen64plusae.util.*;
import paulscode.android.mupen64plusae.persistent.ConfigFile;
import paulscode.android.mupen64plusae.task.ExtractAssetsOrCleanupTask;

class WaveRaceHost {
    static final String MD5 = "EFDE606C824DAACF715928B914AC26E0";
    static final String BASE = "FF67DF97476C210D158779AE6142F239";
    static final String KEY = "mupen64plus_data/mupen64plus.ini";
    static final String CRC = "57AF88CE EDE723DA";
    static final String NAME = "Wave Race 64 - Shindou Edition (J) (English translation)";
    static void check(boolean ok, String message) {
        if (!ok) throw new AssertionError(message);
    }
    static String saveType(RomDatabase.RomDetail d) throws Exception {
        Field f = d.getClass().getDeclaredField("saveType");
        f.setAccessible(true);
        return (String)f.get(d);
    }
    public static void main(String[] args) throws Exception {
        Path asset = Paths.get(args[1], KEY);
        ConfigFile config = new ConfigFile(asset.toString());
        check(BASE.equals(config.get(MD5, "RefMD5")), "translation references original");
        check("3".equals(config.get(BASE, "CountPerOp")), "original CountPerOp=3");
        check(CRC.equals(config.get(MD5, "CRC")), "translated CRC");
        RomDatabase db = RomDatabase.getInstance();
        db.setDatabaseFile(asset.toString());
        RomDatabase.RomDetail translated = db.lookupByMd5WithFallback(MD5, "renamed.z64", "00000000 00000000", CountryCode.JAPAN);
        RomDatabase.RomDetail base = db.lookupByMd5WithFallback(BASE, "original.z64", "", CountryCode.JAPAN);
        check(NAME.equals(translated.goodName) && MD5.equals(translated.md5), "MD5 lookup ignores filename/CRC");
        check(Objects.equals(saveType(base),saveType(translated)), "inherited save type");
        check(base.players == translated.players && base.rumble == translated.rumble && base.status == translated.status, "inherited frontend metadata");
        RomDatabase.RomDetail fallback = db.lookupByMd5WithFallback("00000000000000000000000000000000", "other.z64", CRC, CountryCode.JAPAN);
        check(NAME.equals(fallback.goodName), "existing unknown-MD5 CRC fallback");
        RomDatabase.RomDetail unknown = db.lookupByMd5WithFallback("00000000000000000000000000000000", "unknown.z64", "00000001 00000002", CountryCode.JAPAN);
        check("unknown".equals(unknown.goodName) && unknown.md5.isEmpty(), "unknown filename fallback");
        System.out.println("PASS production Java ConfigFile + RomDatabase lookup/inheritance/CRC fallback");

        Path installed = Paths.get(args[0], "installed");
        Files.createDirectories(installed);
        Context context = new Context();
        SharedPreferences prefs = context.preferences;
        Field versionsField = ExtractAssetsOrCleanupTask.class.getDeclaredField("mAssetVersions");
        versionsField.setAccessible(true);
        @SuppressWarnings("unchecked")
        Map<String,Integer> versions = (Map<String,Integer>)versionsField.get(null);
        check(Integer.valueOf(14).equals(versions.get(KEY)), "DB asset version must be 14");
        for (Map.Entry<String,Integer> entry : versions.entrySet()) {
            String name = entry.getKey().substring("mupen64plus_data/".length());
            Files.copy(Paths.get(args[1], entry.getKey()), installed.resolve(name));
            prefs.edit().putInt(entry.getKey(), entry.getValue()).apply();
        }
        Path installedDb = installed.resolve("mupen64plus.ini");
        Files.writeString(installedDb, "; stale installed version 13\n");
        prefs.edit().putInt(KEY,13).putInt("appVersion",332).apply();
        check(!ExtractAssetsOrCleanupTask.areAllAssetsValid(prefs,"mupen64plus_data",installed.toString()), "same-app-version installed 13 must invalidate");
        AssetManager manager = new AssetManager(args[1]);
        ExtractAssetsOrCleanupTask task = new ExtractAssetsOrCleanupTask(context, manager, null, null, "mupen64plus_data", installed.toString(),
            new ExtractAssetsOrCleanupTask.ExtractAssetsListener() {
                public void onExtractAssetsProgress(String s,int current,int total) {}
                public void onExtractAssetsFinished(List<ExtractAssetsOrCleanupTask.Failure> failures) {}
            });
        // Exercise real selective extraction without unrelated legacy cleanup.
        Method extract = ExtractAssetsOrCleanupTask.class.getDeclaredMethod("extractAssets",List.class,String.class,String.class);
        extract.setAccessible(true);
        List<ExtractAssetsOrCleanupTask.Failure> failures = new ArrayList<>();
        extract.invoke(task, failures,"mupen64plus_data",installed.toString());
        check(failures.isEmpty() && manager.opens == 1, "only stale database extracted");
        check(Files.mismatch(asset,installedDb) == -1, "installed bytes match bundled database");
        check(prefs.getInt(KEY,0) == 14 && prefs.getInt("appVersion",0) == 332, "DB refresh independently of app version");
        check(ExtractAssetsOrCleanupTask.areAllAssetsValid(prefs,"mupen64plus_data",installed.toString()), "version 14 valid");
        extract.invoke(task, failures,"mupen64plus_data",installed.toString());
        check(manager.opens == 1, "version 14 must not re-extract");
        Files.delete(installedDb);
        check(!ExtractAssetsOrCleanupTask.areAllAssetsValid(prefs,"mupen64plus_data",installed.toString()), "missing file invalid even at 14");
        extract.invoke(task, failures,"mupen64plus_data",installed.toString());
        check(failures.isEmpty() && manager.opens == 2 && Files.mismatch(asset,installedDb) == -1, "missing file repaired");
        System.out.println("PASS production asset gate/extraction: installed 13 -> 14, appVersion unchanged, no refresh at 14, missing file repair");
    }
}