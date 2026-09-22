// Temporary host platform stubs only. Production parsers/extractor are compiled
// unchanged. Source-contract checks below are explicitly not runtime tests.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const work = process.argv[2];
function write(name, content) {
  const file = path.join(work, name);
  fs.mkdirSync(path.dirname(file), {recursive: true});
  fs.writeFileSync(file, content);
}
write('include/zlib.h', '/* Unused osal gzip declaration; no gzip code linked. */\ntypedef void *gzFile;\n');
const sources = {
  'android/text/TextUtils': 'public class TextUtils { public static boolean isEmpty(CharSequence s) { return s == null || s.length() == 0; } }',
  'android/util/Log': 'public class Log { public static int e(String t,String m){System.err.println(t+": "+m);return 0;} public static int w(String t,String m){return 0;} public static int i(String t,String m){return 0;} }',
  'androidx/annotation/Nullable': 'public @interface Nullable {}',
  'android/content/SharedPreferences': `public class SharedPreferences {
    private final java.util.Map<String,Integer> data = new java.util.HashMap<>();
    public int getInt(String key,int def){return data.getOrDefault(key,def);}
    public Editor edit(){return new Editor();}
    public class Editor { public Editor putInt(String key,int value){data.put(key,value);return this;} public void apply(){} }
  }`,
  'android/content/Context': `public class Context {
    public final SharedPreferences preferences = new SharedPreferences();
    public android.content.res.AssetManager getAssets(){throw new UnsupportedOperationException();}
  }`,
  'android/content/res/AssetManager': `public class AssetManager {
    private final java.nio.file.Path root;
    public int opens;
    public AssetManager(String root){this.root=java.nio.file.Paths.get(root);}
    public java.io.InputStream open(String file) throws java.io.IOException {opens++; return java.nio.file.Files.newInputStream(root.resolve(file));}
    public String[] list(String folder) throws java.io.IOException {return root.resolve(folder).toFile().list();}
  }`,
  'androidx/preference/PreferenceManager': `public class PreferenceManager {
    public static android.content.SharedPreferences getDefaultSharedPreferences(android.content.Context c){return c.preferences;}
  }`,
  'paulscode/android/mupen64plusae/util/FileUtil': `public class FileUtil {
    public static void makeDirs(String path){new java.io.File(path).mkdirs();}
    public static boolean copyFile(java.io.File a,java.io.File b,boolean move){throw new UnsupportedOperationException("cleanup outside test scope");}
    public static void deleteFolder(java.io.File f){throw new UnsupportedOperationException("cleanup outside test scope");}
    public static void deleteExtensionFolder(java.io.File f,String ext){throw new UnsupportedOperationException("cleanup outside test scope");}
  }`,
  'paulscode/android/mupen64plusae/persistent/AppData': 'public class AppData {public String gameDataDir,legacyGameDataDir;}',
  'paulscode/android/mupen64plusae/persistent/GamePrefs': 'public class GamePrefs {public static String removeInvalidCharacters(String s){throw new UnsupportedOperationException("cleanup outside test scope");}}',
  'paulscode/android/mupen64plusae/persistent/GlobalPrefs': `public class GlobalPrefs {
    public boolean useExternalStorge;
    public String coverArtDir,legacyCoreConfigDir,legacyCoverArtDir,legacyProfilesDir,legacyRomInfoCacheCfg,legacyTouchscreenCustomSkinsDir,profilesDir,romInfoCacheCfg,shaderCacheDir,touchscreenCustomSkinsDir;
  }`
};
for (const [name, body] of Object.entries(sources)) {
  write('src/'+name+'.java', 'package '+name.slice(0,name.lastIndexOf('/')).replaceAll('/','.')+';\n'+body);
}
const javaRoot = 'app/src/main/java/paulscode/android/mupen64plusae/';
const splash = fs.readFileSync(javaRoot+'SplashActivity.java','utf8');
assert.match(splash, /getAppVersion\(\)\s*!=\s*mAppData\.appVersionCode\s*\|\|\s*!ExtractAssetsOrCleanupTask\.areAllAssetsValid/);
const main = fs.readFileSync('mupen64plus-core/upstream/src/main/main.c','utf8');
assert.match(main, /count_per_op = ConfigGetParamInt\(g_CoreConfig, "CountPerOp"\)/);
assert.match(main, /if \(count_per_op <= 0\)\s*count_per_op = ROM_SETTINGS\.countperop;/);
assert.match(main, /ConfigSetDefaultInt\(g_CoreConfig, "CountPerOp", 0,/);
// Compile the exact production fallback statement as a focused seam, not an
// independently reimplemented policy. Full main.c/device boot is not linked.
const policy = main.match(/if \(count_per_op <= 0\)\s*count_per_op = ROM_SETTINGS\.countperop;/)[0];
write('include/wave-race-policy.h',
  'static uint32_t production_cpo_fallback(uint32_t count_per_op) {\n'+policy+'\nreturn count_per_op;\n}\n');
const prefs = fs.readFileSync(javaRoot+'persistent/GamePrefs.java','utf8');
assert.match(prefs, /gameDataDir = getGameDataPath\( romMd5, headerName, countrySymbol\);/);
assert.match(prefs, /mSharedPrefsName = romMd5\.replace\(' ', '_' \) \+ "_preferences";/);
assert.match(prefs, /return String\.format\( "%s %s %s", headerName, countrySymbol, romMd5 \);/);
assert.match(prefs, /return String\.format\( "%s", romMd5 \);/);
console.log('PASS SOURCE CONTRACTS (not runtime): explicit native CountPerOp override remains authoritative; save paths use header/MD5, not GoodName; Splash OR-checks asset validity even at same app version');