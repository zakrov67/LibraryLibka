#include "Bebik.h"
#include <link.h>
#include <dlfcn.h>

// Добавлены стандартные заголовки для безопасности компиляции
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <thread>
#include <mutex>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cstdio>

struct android_app;
extern android_app *g_App;
extern bool g_MenuOpen;
extern volatile bool bValid;

android_app *g_App = 0;

template <typename T> inline T Clamp(T val, T minVal, T maxVal) {
  return val < minVal ? minVal : (val > maxVal ? maxVal : val);
}

inline float UnwindDegrees(float deg) {
  while (deg > 180.f)
    deg -= 360.f;
  while (deg < -180.f)
    deg += 360.f;
  return deg;
}

struct GLTexture {
  GLuint id = 0;
  int w = 0;
  int h = 0;
  bool ok = false;
};

static GLTexture LoadTextureFromMemory(const unsigned char *data, int dataLen) {
  GLTexture tex;
  int channels = 0;
  unsigned char *pixels = stbi_load_from_memory(data, dataLen, &tex.w, &tex.h,
                        &channels, STBI_rgb_alpha);
  if (!pixels)
    return tex;

  glGenTextures(1, &tex.id);
  glBindTexture(GL_TEXTURE_2D, tex.id);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex.w, tex.h, 0, GL_RGBA,
              GL_UNSIGNED_BYTE, pixels);
  glBindTexture(GL_TEXTURE_2D, 0);
  stbi_image_free(pixels);
  tex.ok = true;
  return tex;
}

static GLTexture g_BgTex;
static GLTexture g_LogoTex;

enum class LoginState { IDLE, LOADING, SUCCESS, ERROR_MSG };
static LoginState g_LoginState = LoginState::IDLE;
static std::mutex g_LoginMutex;
static std::thread g_LoginThread;

enum Language { ENG, RU };
static Language current_lang = ENG;

#define SUCHKA(RET, NAME, ARGS)                                                \
  RET(*o##NAME)                                                                \
  ARGS;                                                                        \
  RET h##NAME ARGS


#include "Offset/Offset.hpp"

using json = nlohmann::json;
#include "offset/SDK.hpp"
using namespace SDK;

float lerp(float a, float b, float t) { return a + t * (b - a); }

enum class EAimTrigger {
  None     = 0,
  Shooting = 1,
  Scoping  = 2,
  Both     = 3,
  Any      = 4
};

enum class EAimTarget {
  Head   = 0,
  Neck   = 1,
  Body   = 2,
  Pelvis = 3
};

using AimMode    = EAimTarget;
using AimTrigger = EAimTrigger;

static const char *aimTriggerLabels[][2] = {
    {"None",             "Нет"},
    {"Shooting",         "Стрельба"},
    {"Scoping",          "Прицел"},
    {"Both",             "Оба"},
    {"Any",              "Любой"},
};

static const char *aimModeLabels[][2] = {
    {"Head",   "Голова"},
    {"Neck",   "Шея"},
    {"Body",   "Тело"},
    {"Pelvis", "Таз"},
};

struct sConfig {
  struct sESP {
    bool Box;
    bool Line;
    bool Name;
    bool Distance;
    bool TeamID;
    bool Health;
    bool Skeleton;
    bool Airdrop;
    bool Dogs;
    bool Lootbox;
    bool Alert;
    bool HideBot;
    bool Grenades;
    bool Vehicle;
  };
  sESP ESP{};

  struct sAimBot {
    bool VisCheck = true;
    bool Enable;
    bool IgnoreBots;
    bool IgnoreKnocked;
    float Distance;
    float Cross;
    EAimTarget Target  = EAimTarget::Head;
    EAimTrigger Trigger = EAimTrigger::None;
    float RecoilControl;
    float Smooth = 1.0f;
    bool Prediction = false;
    float Gravity = -0.0098f;
    float Accuracy = 1.0f;
    float SmoothX = 1.0;
    float SmoothY = 1.0;
  };
  sAimBot AimBot{};

  struct sMemory {
    bool IPad;
    float IPadSize;
    bool Speedhack;
    bool Unlock;
  };
  sMemory Memory{};
};
sConfig Config{};

void SALVADOR(FRotator &angles) {
    if (angles.Pitch > 180)
        angles.Pitch -= 360;
    if (angles.Pitch < -180)
        angles.Pitch += 360;
    if (angles.Pitch < -75.f)
        angles.Pitch = -75.f;
    else if (angles.Pitch > 75.f)
        angles.Pitch = 75.f;
    while (angles.Yaw < -180.0f)
        angles.Yaw += 360.0f;
    while (angles.Yaw > 180.0f)
        angles.Yaw -= 360.0f;
}

#include "Secure/base64/base64.h"
#include <ctime>
#include <dirent.h>
#include <curl/curl.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/hmac.h>

static std::string GetGameVersion(JNIEnv *env, jobject context) {
  jclass contextCls = env->FindClass("android/content/Context");
  jmethodID getPM = env->GetMethodID(contextCls, "getPackageManager",
                        "()Landroid/content/pm/PackageManager;");
  jobject pm = env->CallObjectMethod(context, getPM);

  jclass pmCls = env->FindClass("android/content/pm/PackageManager");
  jmethodID getPI =
      env->GetMethodID(pmCls, "getPackageInfo",
                      "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;");

  jclass ctxCls2 = env->FindClass("android/content/Context");
  jmethodID getPkgName =
      env->GetMethodID(ctxCls2, "getPackageName", "()Ljava/lang/String;");
  jstring pkgName = (jstring)env->CallObjectMethod(context, getPkgName);

  jobject pi = env->CallObjectMethod(pm, getPI, pkgName, (jint)0);
  if (!pi)
    return "unknown";

  jclass piCls = env->FindClass("android/content/pm/PackageInfo");
  jfieldID verNameFld =
      env->GetFieldID(piCls, "versionName", "Ljava/lang/String;");
  jstring verName = (jstring)env->GetObjectField(pi, verNameFld);
  if (!verName)
    return "unknown";

  const char *ver = env->GetStringUTFChars(verName, 0);
  std::string result = ver ? ver : "unknown";
  env->ReleaseStringUTFChars(verName, ver);
  return result;
}

static std::string GetConfigPath() {
  if (!g_App || !g_App->activity)
    return "";
  JNIEnv *env = nullptr;
  g_App->activity->vm->AttachCurrentThread(&env, nullptr);

  jobject context = g_App->activity->clazz;
  jclass ctxCls = env->FindClass("android/content/Context");
  jmethodID getPkgName =
      env->GetMethodID(ctxCls, "getPackageName", "()Ljava/lang/String;");
  jstring pkgJs = (jstring)env->CallObjectMethod(context, getPkgName);
  const char *pkg = env->GetStringUTFChars(pkgJs, 0);
  std::string pkgStr = pkg ? pkg : "com.tencent.ig";
  env->ReleaseStringUTFChars(pkgJs, pkg);

  g_App->activity->vm->DetachCurrentThread();

  std::string dir = "/sdcard/Android/data/" + pkgStr + "/files";
  mkdir(dir.c_str(), 0777);
  return dir + "/kyber.json";
}

static std::string GetKeyFilePath() {
  if (!g_App || !g_App->activity)
    return "";
  JNIEnv *env = nullptr;
  g_App->activity->vm->AttachCurrentThread(&env, nullptr);
  jobject context = g_App->activity->clazz;
  jclass ctxCls = env->FindClass("android/content/Context");
  jmethodID getPkgName =
      env->GetMethodID(ctxCls, "getPackageName", "()Ljava/lang/String;");
  jstring pkgJs = (jstring)env->CallObjectMethod(context, getPkgName);
  const char *pkg = env->GetStringUTFChars(pkgJs, 0);
  std::string pkgStr = pkg ? pkg : "com.tencent.ig";
  env->ReleaseStringUTFChars(pkgJs, pkg);
  g_App->activity->vm->DetachCurrentThread();
  const char* internalPath = g_App->activity->internalDataPath;
  if (internalPath && internalPath[0] != '\0')
    return std::string(internalPath) + "/.k";
  return "/sdcard/Android/data/" + pkgStr + "/.k";
}

static void DeleteKeyFile() {
  std::string path = GetKeyFilePath();
  if (!path.empty())
    remove(path.c_str());
}

const char *GetAndroidID(JNIEnv *env, jobject context);
const char *GetDeviceModel(JNIEnv *env);
const char *GetDeviceBrand(JNIEnv *env);
__attribute__((noinline)) void _CheckValid();
static bool _CV_IsValid();
__attribute__((noinline)) void _SetValid(bool val);

static const char PINNED_CERT_FINGERPRINT[] =
    "f1:mo:dk:ey:ho:st:00:00:00:00:00:00:00:00:00:00:"
    "00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00";

#include <openssl/evp.h>
#include <openssl/rand.h>

static const uint8_t AES_KEY_BYTES[32] = {
    0xA3^0x11, 0x7F^0x22, 0x2C^0x33, 0xD1^0x44,
    0x88^0x55, 0x4E^0x66, 0xB5^0x77, 0x09^0x88,
    0xCC^0x99, 0xF3^0xAA, 0x71^0xBB, 0xAA^0xCC,
    0x56^0xDD, 0xDE^0xEE, 0x12^0xFF, 0x90^0x11,
    0x3B^0x22, 0xE7^0x33, 0x44^0x44, 0x8D^0x55,
    0x2F^0x66, 0x61^0x77, 0x97^0x88, 0x05^0x99,
    0xBC^0xAA, 0xFE^0xBB, 0x30^0xCC, 0x74^0xDD,
    0x1C^0xEE, 0x6B^0xFF, 0xD9^0x11, 0x58^0x22
};

static std::string _AES256_Encrypt(const std::string &plaintext) {
    uint8_t iv[16];
    if (RAND_bytes(iv, sizeof(iv)) != 1)
        return "";

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return "";

    std::string out;
    out.resize(plaintext.size() + 32 + 16);
    int outLen1 = 0, outLen2 = 0;

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, AES_KEY_BYTES, iv) != 1) {
        EVP_CIPHER_CTX_free(ctx); return "";
    }
    uint8_t *buf = (uint8_t *)out.data() + 16;
    if (EVP_EncryptUpdate(ctx, buf, &outLen1,
                        (const uint8_t *)plaintext.data(), (int)plaintext.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx); return "";
    }
    if (EVP_EncryptFinal_ex(ctx, buf + outLen1, &outLen2) != 1) {
        EVP_CIPHER_CTX_free(ctx); return "";
    }
    EVP_CIPHER_CTX_free(ctx);

    memcpy(&out[0], iv, 16);
    out.resize(16 + outLen1 + outLen2);

    std::string hex;
    hex.reserve(out.size() * 2);
    char hbuf[3];
    for (uint8_t c : out) {
        snprintf(hbuf, sizeof(hbuf), "%02X", c);
        hex += hbuf;
    }
    return hex;
}

static std::string _AES256_Decrypt(const std::string &hexIn) {
    if (hexIn.size() % 2 != 0 || hexIn.size() < 32 + 2)
        return "";

    std::string raw;
    raw.reserve(hexIn.size() / 2);
    for (size_t i = 0; i < hexIn.size(); i += 2) {
        char buf[3] = {hexIn[i], hexIn[i+1], 0};
        raw += (char)strtol(buf, nullptr, 16);
    }
    if (raw.size() < 32) return "";

    uint8_t iv[16];
    memcpy(iv, raw.data(), 16);
    const uint8_t *ciphertext = (const uint8_t *)raw.data() + 16;
    int cipherLen = (int)(raw.size() - 16);

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return "";

    std::string plain;
    plain.resize(cipherLen + 16);
    int outLen1 = 0, outLen2 = 0;

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, AES_KEY_BYTES, iv) != 1) {
        EVP_CIPHER_CTX_free(ctx); return "";
    }
    if (EVP_DecryptUpdate(ctx, (uint8_t *)plain.data(), &outLen1, ciphertext, cipherLen) != 1) {
        EVP_CIPHER_CTX_free(ctx); return "";
    }
    if (EVP_DecryptFinal_ex(ctx, (uint8_t *)plain.data() + outLen1, &outLen2) != 1) {
        EVP_CIPHER_CTX_free(ctx); return "";
    }
    EVP_CIPHER_CTX_free(ctx);
    plain.resize(outLen1 + outLen2);
    return plain;
}

static std::string _DeviceHMAC(const std::string &data) {
  if (!g_App || !g_App->activity) return "";
  JNIEnv *env = nullptr;
  g_App->activity->vm->AttachCurrentThread(&env, nullptr);
  std::string androidID = GetAndroidID(env, g_App->activity->clazz);
  g_App->activity->vm->DetachCurrentThread();
  if (androidID.empty()) return "";

  unsigned char hmac[32];
  unsigned int  hmacLen = 0;
  HMAC(EVP_sha256(),
      androidID.data(), (int)androidID.size(),
      (const unsigned char*)data.data(), (int)data.size(),
      hmac, &hmacLen);

  std::string hex;
  hex.reserve(64);
  char hbuf[3];
  for (unsigned int i = 0; i < hmacLen; i++) {
    snprintf(hbuf, sizeof(hbuf), "%02X", hmac[i]);
    hex += hbuf;
  }
  return hex;
}

static void SaveKey(const std::string &key) {
  std::string path = GetKeyFilePath();
  if (path.empty() || key.empty())
    return;
  std::string encrypted = _AES256_Encrypt(key);
  if (encrypted.empty())
    return;
  std::string tag = _DeviceHMAC(encrypted);
  std::string payload = tag.empty() ? encrypted : (encrypted + ":" + tag);
  FILE *f = fopen(path.c_str(), "w");
  if (f) {
    fwrite(payload.c_str(), 1, payload.size(), f);
    fclose(f);
  }
}

static std::string LoadKey() {
  std::string path = GetKeyFilePath();
  if (path.empty())
    return "";
  FILE *f = fopen(path.c_str(), "r");
  if (!f)
    return "";
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  rewind(f);
  if (sz <= 0 || sz > 8192) {
    fclose(f);
    return "";
  }
  std::string buf(sz, '\0');
  fread(&buf[0], 1, sz, f);
  fclose(f);
  while (!buf.empty() && (buf.back() == '\n' || buf.back() == '\r' || buf.back() == ' '))
    buf.pop_back();

  auto sep = buf.rfind(':');
  if (sep == std::string::npos) {
    return "";
  }
  std::string encHex = buf.substr(0, sep);
  std::string storedTag = buf.substr(sep + 1);

  std::string expectedTag = _DeviceHMAC(encHex);
  if (expectedTag.empty() || expectedTag != storedTag) {
    remove(path.c_str());
    return "";
  }

  std::string decrypted = _AES256_Decrypt(encHex);
  return decrypted;
}

static std::string _ConfigSign(const std::string &data) {
  unsigned char hash[32];
  unsigned int  hlen = 32;
  HMAC(EVP_sha256(),
      AES_KEY_BYTES, sizeof(AES_KEY_BYTES),
      (const unsigned char *)data.data(), data.size(),
      hash, &hlen);
  char hexbuf[65];
  for (int i = 0; i < 32; i++)
    snprintf(hexbuf + i * 2, 3, "%02x", hash[i]);
  hexbuf[64] = 0;
  return std::string(hexbuf);
}

static bool _ConfigVerify(const std::string &data, const std::string &sig) {
  std::string expected = _ConfigSign(data);
  if (expected.size() != sig.size()) return false;
  int diff = 0;
  for (size_t i = 0; i < expected.size(); i++)
    diff |= (expected[i] ^ sig[i]);
  return diff == 0;
}
static void SaveConfig() {
  std::string path = GetConfigPath();
  if (path.empty())
    return;

  _CheckValid();
  if (!_CV_IsValid())
    return;

  json cfg;
  cfg["ESP"]["Box"] = Config.ESP.Box;
  cfg["ESP"]["Line"] = Config.ESP.Line;
  cfg["ESP"]["Name"] = Config.ESP.Name;
  cfg["ESP"]["Distance"] = Config.ESP.Distance;
  cfg["ESP"]["TeamID"] = Config.ESP.TeamID;
  cfg["ESP"]["Health"] = Config.ESP.Health;
  cfg["ESP"]["Skeleton"] = Config.ESP.Skeleton;
  cfg["ESP"]["Airdrop"] = Config.ESP.Airdrop;
  cfg["ESP"]["Dogs"] = Config.ESP.Dogs;
  cfg["ESP"]["Lootbox"] = Config.ESP.Lootbox;
  cfg["ESP"]["Alert"] = Config.ESP.Alert;
  cfg["ESP"]["HideBot"] = Config.ESP.HideBot;
  cfg["ESP"]["Grenades"] = Config.ESP.Grenades;
  cfg["ESP"]["Vehicle"] = Config.ESP.Vehicle;
  cfg["AimBot"]["Enable"] = Config.AimBot.Enable;
  cfg["AimBot"]["IgnoreBots"] = Config.AimBot.IgnoreBots;
  cfg["AimBot"]["IgnoreKnocked"] = Config.AimBot.IgnoreKnocked;
  cfg["AimBot"]["Distance"] = Config.AimBot.Distance;
  cfg["AimBot"]["Cross"] = Config.AimBot.Cross;
  cfg["AimBot"]["RecoilControl"] = Config.AimBot.RecoilControl;
  cfg["AimBot"]["Smooth"]        = Config.AimBot.Smooth;
  cfg["AimBot"]["Target"]  = (int)Config.AimBot.Target;
  cfg["AimBot"]["Trigger"] = (int)Config.AimBot.Trigger;
  cfg["Memory"]["IPad"] = Config.Memory.IPad;
  cfg["Memory"]["IPadSize"] = Config.Memory.IPadSize;
  cfg["Memory"]["Speedhack"] = Config.Memory.Speedhack;
  cfg["Memory"]["Unlock"] = Config.Memory.Unlock;
  cfg["Lang"] = (int)current_lang;

  std::string plainJson = cfg.dump(2);
  std::string encJson   = _AES256_Encrypt(plainJson);
  std::fill(plainJson.begin(), plainJson.end(), 0);
  if (encJson.empty()) return;

  std::string sig = _ConfigSign(encJson);
  std::ofstream f(path);
  if (f.is_open()) {
    f << encJson << ":" << sig;
    f.close();
  }
}

static void LoadConfig() {
  std::string path = GetConfigPath();
  if (path.empty())
    return;

  {
    std::ifstream f(path);
    if (!f.is_open())
      return;

    std::string encHex((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
    while (!encHex.empty() && (encHex.back() == '\n' || encHex.back() == '\r' || encHex.back() == ' '))
      encHex.pop_back();

    std::string encData, sigData;
    auto sepPos = encHex.rfind(':');
    if (sepPos == std::string::npos || encHex.size() - sepPos - 1 != 64) {
          return;
    }
    encData = encHex.substr(0, sepPos);
    sigData = encHex.substr(sepPos + 1);
    if (!_ConfigVerify(encData, sigData)) {
          return;
    }

    std::string plainJson = _AES256_Decrypt(encData);
    if (plainJson.empty()) { return; }

    try {
      json cfg = json::parse(plainJson);
      std::fill(plainJson.begin(), plainJson.end(), 0);
      if (cfg.contains("ESP")) {
        auto &e = cfg["ESP"];
        if (e.contains("Box"))      Config.ESP.Box      = e["Box"];
        if (e.contains("Line"))     Config.ESP.Line     = e["Line"];
        if (e.contains("Name"))     Config.ESP.Name     = e["Name"];
        if (e.contains("Distance")) Config.ESP.Distance = e["Distance"];
        if (e.contains("TeamID"))   Config.ESP.TeamID   = e["TeamID"];
        if (e.contains("Health"))   Config.ESP.Health   = e["Health"];
        if (e.contains("Skeleton")) Config.ESP.Skeleton = e["Skeleton"];
        if (e.contains("Airdrop"))  Config.ESP.Airdrop  = e["Airdrop"];
        if (e.contains("Dogs"))     Config.ESP.Dogs     = e["Dogs"];
        if (e.contains("Lootbox"))  Config.ESP.Lootbox  = e["Lootbox"];
        if (e.contains("Alert"))    Config.ESP.Alert    = e["Alert"];
        if (e.contains("HideBot"))  Config.ESP.HideBot  = e["HideBot"];
        if (e.contains("Grenades")) Config.ESP.Grenades = e["Grenades"];
        if (e.contains("Vehicle"))  Config.ESP.Vehicle  = e["Vehicle"];
      }
      if (cfg.contains("AimBot")) {
        auto &a = cfg["AimBot"];
        if (a.contains("Enable"))        Config.AimBot.Enable        = a["Enable"];
        if (a.contains("IgnoreBots"))    Config.AimBot.IgnoreBots    = a["IgnoreBots"];
        if (a.contains("IgnoreKnocked")) Config.AimBot.IgnoreKnocked = a["IgnoreKnocked"];
        if (a.contains("Distance"))      Config.AimBot.Distance      = a["Distance"];
        if (a.contains("Cross"))         Config.AimBot.Cross         = a["Cross"];
        if (a.contains("RecoilControl")) Config.AimBot.RecoilControl = a["RecoilControl"];
        if (a.contains("Smooth"))        Config.AimBot.Smooth        = a["Smooth"];
        if (a.contains("Target"))         Config.AimBot.Target         = (EAimTarget)(int)a["Target"];
        if (a.contains("Trigger"))       Config.AimBot.Trigger       = (EAimTrigger)(int)a["Trigger"];
      }
      if (cfg.contains("Memory")) {
        auto &m = cfg["Memory"];
        if (m.contains("IPad"))       Config.Memory.IPad       = m["IPad"];
        if (m.contains("IPadSize"))   Config.Memory.IPadSize   = m["IPadSize"];
        if (m.contains("Speedhack"))  Config.Memory.Speedhack  = m["Speedhack"];
        if (m.contains("Unlock"))     Config.Memory.Unlock     = m["Unlock"];
      }
      if (cfg.contains("Lang"))
        current_lang = (Language)(int)cfg["Lang"];
    } catch (...) {
    }
  }
}

static void OnConfigChanged() { SaveConfig(); }

volatile bool bValid = false;
static bool g_MenuOpen = false;
static bool g_CrackDetected = false;
static std::string g_KeyExpiry = "";
static std::string configFileName = "UNFAIL-Cfg.ini";
static std::string currentConfigName = "";
static const uint8_t _xor_url[] = {
  0x32, 0x2E, 0x2E, 0x2A, 0x29, 0x60, 0x75, 0x75,
  0x3C, 0x6B, 0x74, 0x37, 0x35, 0x3E, 0x31, 0x3F,
  0x23, 0x74, 0x32, 0x35, 0x29, 0x2E, 0x75, 0x62,
  0x68, 0x63, 0x75, 0x39, 0x35, 0x34, 0x34, 0x3F,
  0x39, 0x2E, 0x00
};
static const uint8_t _xor_post[] = {
  0x3D, 0x3B, 0x37, 0x3F, 0x67, 0x0A, 0x0F, 0x18,
  0x1D, 0x7C, 0x2F, 0x29, 0x3F, 0x28, 0x05, 0x31,
  0x3F, 0x23, 0x67, 0x7F, 0x29, 0x7C, 0x29, 0x3F,
  0x28, 0x33, 0x3B, 0x36, 0x67, 0x7F, 0x29, 0x00
};
static std::string _DecXOR(const uint8_t *data, uint8_t key = 0x5A) {
  std::string r;
  for (int i = 0; data[i] != 0; i++) r += (char)(data[i] ^ key);
  return r;
}
void loadConfigBin() {
  const std::string paths[] = {
    "/storage/emulated/0/Android/data/com.tencent.ig/files/" + configFileName,
    "/storage/emulated/0/Android/data/com.pubg.krmobile/files/" + configFileName,
    "/storage/emulated/0/Android/data/com.rekoo.pubgm/files/" + configFileName,
    "/storage/emulated/0/Android/data/com.vng.pubgmobile/files/" + configFileName,
    "/storage/emulated/0/Android/data/com.pubg.imobile/files/" + configFileName
  };
  for (const auto& path : paths) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd != -1) {
      read(fd, &Config, sizeof(Config));
      close(fd);
      currentConfigName = configFileName;
      break;
    }
  }
}
void saveConfigBin() {
  const std::string paths[] = {
    "/storage/emulated/0/Android/data/com.tencent.ig/files/" + configFileName,
    "/storage/emulated/0/Android/data/com.pubg.krmobile/files/" + configFileName,
    "/storage/emulated/0/Android/data/com.rekoo.pubgm/files/" + configFileName,
    "/storage/emulated/0/Android/data/com.vng.pubgmobile/files/" + configFileName,
    "/storage/emulated/0/Android/data/com.pubg.imobile/files/" + configFileName
  };
  for (const auto& path : paths) {
    int fd = open(path.c_str(), O_WRONLY | O_CREAT, 0777);
    if (fd != -1) {
      write(fd, &Config, sizeof(Config));
      close(fd);
      currentConfigName = configFileName;
      break;
    }
  }
}

static volatile uint32_t g_ValidCanary      = 0u;
static volatile uint32_t g_ValidMirror      = 0u;
static volatile uint32_t g_ValidSeed        = 0u;
static volatile uint32_t g_ValidToken       = 0u;
static volatile uint32_t g_AuthEpoch        = 0u;
static volatile uint32_t g_AuthCount        = 0u;
static volatile uint32_t g_AuthCountMirror  = 0xFFFFFFFFu;
static volatile uint32_t g_DeviceHash       = 0u;
static volatile uint32_t g_DeviceHashMirror = 0xFFFFFFFFu;
static volatile uint32_t g_CheckValidCRC    = 0u;
static volatile uint32_t g_CheckValidCRC2   = 0xFFFFFFFFu;

static volatile uint32_t g_CVChallenge  = 0u;
static volatile uint32_t g_CVResponse   = 0u;
static volatile uint32_t g_CVRespMirror = 0xFFFFFFFFu;

static volatile uint32_t g_ValidResultKey   = 0u;
static volatile uint32_t g_ValidResult      = 0u;
static volatile uint32_t g_ValidResult2     = 0u;
#define _RESULT_TRUE  0xA5A5A5A5u
#define _RESULT_FALSE 0x5A5A5A5Au

static __attribute__((noinline)) uint32_t _CRC32Snippet(const void *addr, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  const uint8_t *p = (const uint8_t *)addr;
  for (size_t i = 0; i < len; i++) {
    crc ^= p[i];
    for (int b = 0; b < 8; b++)
      crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
  }
  return crc ^ 0xFFFFFFFFu;
}

static bool _IsFunctionHooked(void *fn) {
  if (!fn) return false;
  uint32_t first_instr = *(volatile uint32_t *)fn;
  uint8_t opcode = (first_instr >> 24) & 0xFF;
  if (opcode == 0x14 || opcode == 0x94 || opcode == 0x17 || opcode == 0x97)
    return true;
  if (first_instr == 0x58000051u) return true;
  return false;
}

// ---------------- ИЗМЕНЕНИЕ 1: Динамическое получение секции .rodata ----------------
static uint32_t _rodata_crc = 0;
static uintptr_t g_RodataStart = 0;
static size_t g_RodataSize = 0;

__attribute__((constructor(105))) static void _InitRodataCRC() {
    Dl_info info;
    if (dladdr((void*)_InitRodataCRC, &info) && info.dli_fname) {
        std::string target_so = info.dli_fname;
        dl_iterate_phdr([](struct dl_phdr_info* info, size_t size, void* data) -> int {
            std::string* target_name = (std::string*)data;
            if (info->dlpi_name && *target_name == info->dlpi_name) {
                for (int i = 0; i < info->dlpi_phnum; i++) {
                    const Elf64_Phdr* phdr = &info->dlpi_phdr[i];
                    // Находим сегмент LOAD с правами только на чтение (без прав на запись)
                    if (phdr->p_type == PT_LOAD && (phdr->p_flags & PF_R) && !(phdr->p_flags & PF_W)) {
                        g_RodataStart = info->dlpi_addr + phdr->p_vaddr;
                        g_RodataSize = phdr->p_memsz;
                        _rodata_crc = _CRC32Snippet((void*)g_RodataStart, g_RodataSize);
                        return 1;
                    }
                }
            }
            return 0;
        }, &target_so);
    }
}

__attribute__((constructor(104))) static void _InitCheckValidCRC() {
  uint32_t crc     = _CRC32Snippet((void *)_CheckValid, 64);
  g_CheckValidCRC  = crc;
  g_CheckValidCRC2 = ~crc;
}

__attribute__((constructor(101))) static void _InitValidSeed() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  uintptr_t stackAddr = (uintptr_t)&ts;
  g_ValidSeed         = (uint32_t)(ts.tv_nsec ^ (stackAddr >> 3) ^ 0xC0FFEE13u);
  g_ValidCanary       = g_ValidSeed;
  g_ValidMirror       = ~g_ValidSeed;
  g_ValidToken        = g_ValidSeed ^ 0xDEADBEEFu;
  g_AuthEpoch         = 0u;
  g_AuthCount         = 0u;
  g_AuthCountMirror   = 0xFFFFFFFFu;
  g_DeviceHash        = 0u;
  g_DeviceHashMirror  = 0xFFFFFFFFu;
  g_CheckValidCRC     = 0u;
  g_CheckValidCRC2    = 0xFFFFFFFFu;
  struct timespec _ts2;
  clock_gettime(CLOCK_MONOTONIC, &_ts2);
  g_ValidResultKey = (uint32_t)(_ts2.tv_nsec ^ (uintptr_t)&g_ValidResultKey ^ 0xF00DCAFE);
  g_ValidResult  = g_ValidResultKey ^ _RESULT_FALSE;
  g_ValidResult2 = ~g_ValidResult;
}

__attribute__((noinline)) void _SetValid(bool val) {
  bValid        = val;
  uint32_t v    = val ? 1u : 0u;
  g_ValidCanary = g_ValidSeed ^ v;
  g_ValidMirror = ~(g_ValidSeed ^ v);
  g_ValidToken  = (g_ValidSeed ^ 0xDEADBEEFu) ^ (v ? 0xFFFFFFFFu : 0u);
  if (val) {
    g_AuthCount++;
    g_AuthCountMirror = ~g_AuthCount;
    if (g_AuthEpoch == 0u) {
      struct timespec _ts;
      clock_gettime(CLOCK_MONOTONIC, &_ts);
      g_AuthEpoch = (uint32_t)(_ts.tv_sec ^ _ts.tv_nsec);
    }
  } else {
    g_AuthCount        = 0u;
    g_AuthCountMirror  = 0xFFFFFFFFu;
    g_DeviceHash       = 0u;
    g_DeviceHashMirror = 0xFFFFFFFFu;
  }
}

__attribute__((noinline)) void _CheckValid() {
  bool _wasValid = (bool)bValid;
  auto _setResult = [_wasValid](bool ok) __attribute__((noinline)) {
    uint32_t enc = (ok ? _RESULT_TRUE : _RESULT_FALSE) ^ g_ValidResultKey;
    g_ValidResult  = enc;
    g_ValidResult2 = ~enc;
    if (!ok && _wasValid) {
      bValid = false;
      g_CrackDetected = true;
    } else if (!ok) {
      bValid = false;
    }
  };

  {
    uintptr_t ra   = (uintptr_t)__builtin_return_address(0);
    uintptr_t self = (uintptr_t)(void*)_CheckValid;
    uintptr_t diff = (ra > self) ? (ra - self) : (self - ra);
    if (diff > 0x1000000u) {
      _setResult(false);
      return;
    }
  }

  bool fromMain = (bool)bValid;
  uint32_t v    = fromMain ? 1u : 0u;

  bool p1 = (g_ValidCanary      == (g_ValidSeed ^ v));
  bool p2 = (g_ValidMirror      == ~(g_ValidSeed ^ v));
  bool p3 = (g_ValidToken       == ((g_ValidSeed ^ 0xDEADBEEFu) ^ (v ? 0xFFFFFFFFu : 0u)));
  bool p4 = (g_AuthCountMirror  == ~g_AuthCount);
  bool p5 = !fromMain || (g_AuthCount > 0u);
  bool p6 = (g_DeviceHashMirror == ~g_DeviceHash);
  bool p7 = !fromMain || (g_DeviceHash != 0u);
  bool p9 = (g_ValidResult2 == ~g_ValidResult);

  bool p8 = true;
  if (g_CheckValidCRC != 0u) {
    uint32_t live = _CRC32Snippet((void *)_CheckValid, 64);
    p8 = (live == g_CheckValidCRC) && (g_CheckValidCRC2 == ~g_CheckValidCRC);
  }

  if (!p1 || !p2 || !p3 || !p4 || !p5 || !p6 || !p7 || !p8 || !p9) {
    bValid            = false;
    g_ValidCanary     = g_ValidSeed;
    g_ValidMirror     = ~g_ValidSeed;
    g_ValidToken      = g_ValidSeed ^ 0xDEADBEEFu;
    g_AuthCount       = 0u;
    g_AuthCountMirror = 0xFFFFFFFFu;
    g_DeviceHash      = 0u;
    g_DeviceHashMirror= 0xFFFFFFFFu;
    _setResult(false);
    return;
  }
  _setResult(fromMain);
  if (fromMain && g_CVChallenge != 0u) {
    uint32_t resp = (g_CVChallenge ^ g_ValidSeed ^ g_DeviceHash ^ 0xABCD1234u);
    resp = (resp << 7) | (resp >> 25);
    resp ^= g_AuthCount;
    g_CVResponse   = resp;
    g_CVRespMirror = ~resp;
  }
}

__attribute__((noinline)) static bool _CV_IsValid() {
  if (g_ValidResult2 != ~g_ValidResult) {
    if (bValid) {
      g_CrackDetected = true;
      bValid = false;
    }
    return false;
  }
  struct timespec _ts;
  clock_gettime(CLOCK_MONOTONIC, &_ts);
  g_CVChallenge = (uint32_t)(_ts.tv_nsec) ^ (uint32_t)(uintptr_t)&_ts
                ^ g_ValidResultKey ^ 0xFEED5EEDu;
  g_CVResponse   = ~g_CVChallenge;
  g_CVRespMirror =  g_CVChallenge;
  _CheckValid();
  uint32_t expected = (g_CVChallenge ^ g_ValidSeed ^ g_DeviceHash ^ 0xABCD1234u);
  expected = (expected << 7) | (expected >> 25);
  expected ^= g_AuthCount;
  bool challenge_ok = (g_CVResponse   == expected)
                  && (g_CVRespMirror  == ~expected);
  if (!challenge_ok) {
    if (bValid) {
      g_CrackDetected = true;
      bValid          = false;
    }
    return false;
  }
  uint32_t dec = g_ValidResult ^ g_ValidResultKey;
  return dec == _RESULT_TRUE;
}

static char g_KeyBuf[64] = {};
static int     g_LoginAttempts   = 0;
static int64_t g_LoginLockUntil  = 0;
static const int   MAX_ATTEMPTS  = 5;
static const int64_t LOCKOUT_MS  = 30000;

static bool _LoginRateLimitCheck() {
  int64_t now = (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  if (now < g_LoginLockUntil) return false;
  return true;
}
static void _LoginRecordAttempt(bool success) {
  if (success) {
    g_LoginAttempts  = 0;
    g_LoginLockUntil = 0;
  } else {
    g_LoginAttempts++;
    if (g_LoginAttempts >= MAX_ATTEMPTS) {
      int64_t now = (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count();
      g_LoginLockUntil = now + LOCKOUT_MS;
      g_LoginAttempts  = 0;
    }
  }
}
static std::string g_LoginError;

#define RAD2DEG(x) ((float)(x) * (float)(180.f / IM_PI))
#define DEG2RAD(x) ((float)(x) * (float)(IM_PI / 180.f))
#ifndef IM_PI
#define IM_PI 3.14159265358979323846f
#endif
#define _ReadStatusReg
#undef ARM64_SYSREG
#define SLEEP_TIME 1000LL / 60LL

void RotateTriangle(std::array<Vector3, 3> &points, float rotation) {
  const auto points_center = (points.at(0) + points.at(1) + points.at(2)) / 3;
  for (auto &point : points) {
    point = point - points_center;
    const auto temp_x = point.X;
    const auto temp_y = point.Y;
    const auto theta = DEG2RAD(rotation);
    const auto c = cosf(theta);
    const auto s = sinf(theta);
    point.X = temp_x * c - temp_y * s;
    point.Y = temp_x * s + temp_y * c;
    point = point + points_center;
  }
}

// ---------------- ИЗМЕНЕНИЕ 2: Исправление WriteAddr ----------------
bool WriteAddr(void *addr, void *buffer, size_t length) {
    unsigned long page_size = sysconf(_SC_PAGESIZE);
    void *start = (void *)((uintptr_t)addr & ~(page_size - 1));
    if (mprotect(start, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        return false;
    }
    memcpy(addr, buffer, length);
    return true;
}
void WriteDword(uintptr_t addr, int var) {
  WriteAddr(reinterpret_cast<void *>(addr), reinterpret_cast<void *>(&var), 4);
}

int setPageProtection(uintptr_t target, int protection) {
  void *start = reinterpret_cast<void *>(target & -PAGE_SIZE);
  return mprotect(start, PAGE_SIZE, protection);
}

ASTExtraPlayerCharacter *g_LocalPlayer = nullptr;
ASTExtraPlayerController *g_PlayerController;

long GetEpochTime() {
  auto duration = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(duration)
      .count();
}
inline uint64_t GetTimeMillis() { return (uint64_t)GetEpochTime(); }

bool g_Initialized = false;
ImGuiWindow *g_window = NULL;

ASTExtraPlayerController *g_LocalController = 0;
bool initImGui = false;
int screenWidth = -1, glWidth, screenHeight = -1, glHeight;
float density = -1;
float fov = 200.0f;
uintptr_t anort;
uintptr_t UE4;
uintptr_t ANOGS;

json items_data;

std::map<int, bool> Items;
std::map<int, float *> ItemColors;

#define CREATE_COLOR(r, g, b, a)                                               \
  new float[4]{(float)r, (float)g, (float)b, (float)a};

#include "Secure/Helper.h"

#ifndef TARGETMODE_DEFINED
static int TargetMode = 0;
static int TargetBone = 0;
static int VisibilityCheck = 0;
static int Trigger = 0;
#endif

void DrawRectWithOutline(ImDrawList *draw, const ImVec2 &start,
                        const ImVec2 &end, ImU32 fillColor, ImU32 outlineColor,
                        float outlineThickness = 2.0f, float rounding = 0.0f,
                        ImDrawFlags flags = 0) {

  ImVec2 outerStart =
      ImVec2(start.x - outlineThickness, start.y - outlineThickness);
  ImVec2 outerEnd = ImVec2(end.x + outlineThickness, end.y + outlineThickness);
  draw->AddRect(outerStart, outerEnd, outlineColor, rounding, flags,
                outlineThickness);

  ImVec2 innerStart =
      ImVec2(start.x + outlineThickness, start.y + outlineThickness);
  ImVec2 innerEnd = ImVec2(end.x - outlineThickness, end.y - outlineThickness);
  draw->AddRect(innerStart, innerEnd, outlineColor, rounding, flags,
                outlineThickness);

  draw->AddRect(start, end, fillColor, rounding, flags, outlineThickness);
}

void DrawLineWithOutline(ImDrawList *draw, const ImVec2 &p1, const ImVec2 &p2,
                        ImU32 color, ImU32 outlineColor, float thickness,
                        float outlineThickness) {
  draw->AddLine(p1, p2, outlineColor, thickness + outlineThickness * 2);
  draw->AddLine(p1, p2, color, thickness);
}

void DrawCircleWithOutline(ImDrawList *draw, const ImVec2 &center, float radius,
                        ImU32 color, ImU32 outlineColor, int num_segments,
                        float thickness, float outlineThickness) {
  draw->AddCircle(center, radius + outlineThickness, outlineColor, num_segments,
                  thickness);
  draw->AddCircle(center, radius, color, num_segments, thickness);
  draw->AddCircle(center, radius - outlineThickness, outlineColor, num_segments,
                  thickness);
}

void DrawOutlinedText(ImDrawList *draw, const char *text, ImVec2 pos,
                      ImU32 textColor, ImU32 outlineColor, float fontSize) {
  ImFont *font = ImGui::GetIO().Fonts->Fonts[0];
  const float originalFontSize = font->FontSize;
  const float scale = fontSize / originalFontSize;

  for (int x = -1; x <= 1; ++x) {
    for (int y = -1; y <= 1; ++y) {
      if (x != 0 || y != 0) {
        draw->AddText(font, fontSize, ImVec2(pos.x + x, pos.y + y),
                      outlineColor, text);
      }
    }
  }
  draw->AddText(font, fontSize, pos, textColor, text);
  font->Scale = 1.0f;
}

void Outlined_Text(float size, int x, int y, ImVec4 color, const char *str) {
  ImGui::GetBackgroundDrawList()->AddText(
      NULL, size, ImVec2(x + 1, y),
      ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 1.0f)), str);
  ImGui::GetBackgroundDrawList()->AddText(
      NULL, size, ImVec2(x - 0.1, y),
      ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 1.0f)), str);
  ImGui::GetBackgroundDrawList()->AddText(
      NULL, size, ImVec2(x, y + 1),
      ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 1.0f)), str);
  ImGui::GetBackgroundDrawList()->AddText(
      NULL, size, ImVec2(x, y - 1),
      ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 1.0f)), str);
  ImGui::GetBackgroundDrawList()->AddText(
      NULL, size, ImVec2(x, y), ImGui::ColorConvertFloat4ToU32(color), str);
}

static void OpenURL(const char *url) {
  if (!g_App || !g_App->activity)
    return;

  ANativeActivity *act = g_App->activity;
  JavaVM *vm = act->vm;
  JNIEnv *env = nullptr;
  vm->AttachCurrentThread(&env, nullptr);

  jclass uriCls = env->FindClass("android/net/Uri");
  jmethodID uriParse = env->GetStaticMethodID(
      uriCls, "parse", "(Ljava/lang/String;)Landroid/net/Uri;");
  jstring jUrl = env->NewStringUTF(url);
  jobject uri = env->CallStaticObjectMethod(uriCls, uriParse, jUrl);

  jclass intentCls = env->FindClass("android/content/Intent");
  jfieldID actionFld =
      env->GetStaticFieldID(intentCls, "ACTION_VIEW", "Ljava/lang/String;");
  jstring action = (jstring)env->GetStaticObjectField(intentCls, actionFld);
  jmethodID intentInit = env->GetMethodID(
      intentCls, "<init>", "(Ljava/lang/String;Landroid/net/Uri;)V");
  jobject intent = env->NewObject(intentCls, intentInit, action, uri);

  jclass actCls = env->GetObjectClass(act->clazz);
  jmethodID startAct =
      env->GetMethodID(actCls, "startActivity", "(Landroid/content/Intent;)V");
  env->CallVoidMethod(act->clazz, startAct, intent);

  env->DeleteLocalRef(jUrl);
  env->DeleteLocalRef(uri);
  env->DeleteLocalRef(intent);
  env->DeleteLocalRef(action);
  vm->DetachCurrentThread();
}

static constexpr uint32_t _FNV1a(const char *s, uint32_t h = 2166136261u) {
  return *s ? _FNV1a(s + 1, (h ^ (uint8_t)*s) * 16777619u) : h;
}
static constexpr uint32_t CHANNEL_URL_HASH =
    _FNV1a("https://t.me/UNFAILMOD");

static void OpenURL_Safe(const char *url) {
  if (_FNV1a(url) != CHANNEL_URL_HASH) {
    const std::string safe_url = ENCLKOVATE("https://t.me/UNFAILMOD");
    OpenURL(safe_url.c_str());
    return;
  }
  OpenURL(url);
}

inline int g_weaponID = 0;

ASTExtraPlayerCharacter* GetTargetByCrossDist() {
    ASTExtraPlayerCharacter* result = nullptr;
    float bestValue = std::numeric_limits<float>::infinity();

    auto Actors = getActors();
    auto* localPlayer = g_LocalPlayer;
    auto* localController = g_LocalController;

    if (!localPlayer || !localController || !localController->PlayerCameraManager) return nullptr;

    FVector2D center = { (float)(glWidth / 2), (float)(glHeight / 2) };

    for (size_t i = 0; i < Actors.size(); i++) {
        auto* Actor = Actors[i];
        if (isObjectInvalid(Actor)) continue;
        if (!Actor->IsA(ASTExtraPlayerCharacter::StaticClass())) continue;

        auto* Player = static_cast<ASTExtraPlayerCharacter*>(Actor);
        if (!Player) continue;
        if (Player->PlayerKey == localPlayer->PlayerKey ||
            Player->TeamID == localPlayer->TeamID ||
            Player->bDead) continue;
        if (Config.AimBot.IgnoreKnocked && Player->Health == 0.0f) continue;
        if (Config.AimBot.IgnoreBots && Player->bEnsure) continue;

        float distance = localPlayer->GetDistanceTo(Player) / 100.f;
        if (!std::isfinite(distance) || distance > Config.AimBot.Distance) continue;

        const char* bones[] = { "Head", "neck_01", "spine_03", "pelvis" };

        int targetIndex = (int)Config.AimBot.Target;
        if (targetIndex < 0 || targetIndex > 3) targetIndex = 0;

        FVector worldPos = Player->GetBonePos(bones[targetIndex], {});
        if (!std::isfinite(worldPos.X) || !std::isfinite(worldPos.Y) || !std::isfinite(worldPos.Z)) continue;

        FVector2D screenPos;
        if (!W2S(worldPos, &screenPos)) continue;

        if (!isInsideFOV((int)screenPos.X, (int)screenPos.Y)) continue;

        float distScreen = FVector2D::Distance(center, FVector2D(screenPos.X, screenPos.Y));
        if (distScreen < bestValue) {
            bestValue = distScreen;
            result = Player;
        }
    }
    return result;
}

void EnhancedAimbot() {
    if (!Config.AimBot.Enable) return;

    auto* localPlayer = g_LocalPlayer;
    auto* localController = g_LocalController;

    if (!localPlayer || !localController || !localController->PlayerCameraManager) return;

    if (!localPlayer->WeaponManagerComponent) return;
    auto* _curWeapon = localPlayer->WeaponManagerComponent->CurrentWeaponReplicated;
    if (!_curWeapon || !_curWeapon->IsA(ASTExtraShootWeapon::StaticClass())) return;

    ASTExtraPlayerCharacter* target = GetTargetByCrossDist();
    if (!target || target->bDead) return;

    bool isADS = localPlayer->bIsGunADS;
    bool isFiring = localPlayer->bIsWeaponFiring;
    bool aimActive = false;

    int triggerType = (int)Config.AimBot.Trigger;

    if (triggerType == (int)EAimTrigger::None) {
        aimActive = true;
    } else if (triggerType == (int)EAimTrigger::Shooting) {
        aimActive = isFiring;
    } else if (triggerType == (int)EAimTrigger::Scoping) {
        aimActive = isADS;
    } else if (triggerType == (int)EAimTrigger::Both) {
        aimActive = isFiring && isADS;
    } else if (triggerType == (int)EAimTrigger::Any) {
        aimActive = isFiring || isADS;
    }

    if (!aimActive) return;

    float bulletSpeed = 750.f;
    if (localPlayer->WeaponManagerComponent) {
        auto* wm = localPlayer->WeaponManagerComponent;
        auto* weapon = (ASTExtraShootWeapon*)wm->CurrentWeaponReplicated;
        if (weapon && weapon->ShootWeaponComponent && weapon->ShootWeaponComponent->ShootWeaponEntityComponent) {
            bulletSpeed = weapon->ShootWeaponComponent->ShootWeaponEntityComponent->BulletFireSpeed;
        }
    }
    if (bulletSpeed < 50.f || !std::isfinite(bulletSpeed)) bulletSpeed = 750.f;

    static const char* BoneList[] = { "Head", "neck_01", "spine_03", "pelvis" };
    int boneIdx = (int)Config.AimBot.Target;
    if (boneIdx < 0 || boneIdx > 3) boneIdx = 0;

    FVector targetAimPos = target->GetBonePos(BoneList[boneIdx], {});
    if (!std::isfinite(targetAimPos.X) || !std::isfinite(targetAimPos.Y) || !std::isfinite(targetAimPos.Z)) return;

    if (Config.AimBot.VisCheck) {
        if (!localController->LineOfSightTo(target, {0, 0, 0}, true)) return;
    }

    if (Config.AimBot.Prediction) {
        FVector Velocity = target->GetVelocity();
        if (std::isfinite(Velocity.X) && std::isfinite(Velocity.Y) && std::isfinite(Velocity.Z)) {
            FVector localPos = localController->PlayerCameraManager->CameraCache.POV.Location;
            FVector relativePos = VectorSubtract(targetAimPos, localPos);

            float a = VectorSizeSquared(Velocity) - bulletSpeed * bulletSpeed;
            float b = 2 * VectorDot(relativePos, Velocity);
            float c = VectorSizeSquared(relativePos);
            float discriminant = b * b - 4 * a * c;

            if (std::isfinite(a) && std::isfinite(b) && std::isfinite(c) && discriminant >= 0) {
                float sqrtDisc = sqrtf(discriminant);
                float t = (-b - sqrtDisc) / (2 * a);
                if (t > 0.f && t < 1.5f) {
                    targetAimPos = VectorAdd(targetAimPos, VectorMultiply(Velocity, t));
                }
            }
        }
    }

    FVector localPos = localController->PlayerCameraManager->CameraCache.POV.Location;

    FRotator camRot = localController->PlayerCameraManager->CameraCache.POV.Rotation;
    FRotator aimRot = ToRotator(localPos, targetAimPos);

    aimRot.Pitch = UnwindDegrees(aimRot.Pitch - camRot.Pitch);
    aimRot.Yaw   = UnwindDegrees(aimRot.Yaw   - camRot.Yaw);

    if (!std::isfinite(aimRot.Pitch) || !std::isfinite(aimRot.Yaw)) return;

    SALVADOR(aimRot);

    float smoothing = std::max(1.0f, Config.AimBot.Smooth);

    FRotator controlRot = localController->ControlRotation;
    controlRot.Pitch += aimRot.Pitch / smoothing;
    controlRot.Yaw   += aimRot.Yaw   / smoothing;

    if (localPlayer && localPlayer->WeaponManagerComponent) {
        auto WeaponManagerComponent = localPlayer->WeaponManagerComponent;

        if (WeaponManagerComponent->CurrentWeaponReplicated) {
            auto currentWeapon = WeaponManagerComponent->CurrentWeaponReplicated;

            if (currentWeapon->IsA(ASTExtraShootWeapon::StaticClass())) {
                auto shootWeapon = (ASTExtraShootWeapon*)currentWeapon;

                if (shootWeapon->ShootWeaponComponent &&
                    shootWeapon->ShootWeaponComponent->ShootWeaponEntityComponent) {
                    shootWeapon->ShootWeaponComponent->ShootWeaponEntityComponent->GameDeviationFactor = 0.0f;
                }

                if (Config.AimBot.Enable) {
                    if (localPlayer->bIsGunADS && localPlayer->bIsWeaponFiring) {
                        float recoilCompensation = Config.AimBot.RecoilControl * 0.1f;
                        controlRot.Pitch -= recoilCompensation;
                        SALVADOR(controlRot);
                    }
                }
            }
        }
    }

    localController->SetControlRotation(controlRot, "");
}

static void _DrawErrorScreen(ImDrawList *bgDraw, int w, int h) {
  bgDraw->AddRectFilled(ImVec2(0, 0), ImVec2((float)w, (float)h), IM_COL32(0, 0, 0, 240));
  ImFont *fnt = ImGui::GetFont();
  float fs = 24.0f;
  const char *err = "ERROR ERROR ERROR POSHEL NAXYU POSHEL NAXYU";
  float tw = ImGui::CalcTextSize(err).x * (fs / fnt->FontSize);
  float th = fs * 1.8f;
  float t = ImGui::GetTime();
  for (float y = 0; y < (float)h; y += th) {
    float offx = fmodf(y * 0.7f + t * 30.0f, tw);
    for (float x = -tw * 2.0f; x < (float)w + tw; x += tw) {
      bgDraw->AddText(fnt, fs, ImVec2(x + offx, y), IM_COL32(255, 0, 0, 255), err);
    }
  }
  float bw = 4.0f;
  bgDraw->AddRect(ImVec2(5, 5), ImVec2((float)w - 5, (float)h - 5),
                    IM_COL32(255, 0, 0, 255), 0.0f, 0, bw);
}
void DrawESP(ImDrawList *draw, int glWidth, int glHeight) {
    _CheckValid();
  if (!_CV_IsValid()) {
    return;
  }

      draw->AddText({((float) density / 12.0f), 18}, ImColor(255, 0, 0, 255), " Telegram Channel -> @UNFAILMOD");

    if (Config.AimBot.Enable) {
        draw->AddCircle(ImVec2(glWidth / 2.0f, glHeight / 2.0f),
                        Config.AimBot.Cross * 0.5f, IM_COL32(255, 0, 0, 255), 100,
                        1.0f);
    }

  int totalEnemies = 0, totalBots = 0;

  auto Actors = getActors();
  ASTExtraPlayerCharacter *localPlayer = 0;
  ASTExtraPlayerController *localController = 0;

  for (auto Actor : Actors) {
    if (isObjectInvalid(Actor))
      continue;

    if (Actor->IsA(ASTExtraPlayerController::StaticClass())) {
      localController = (ASTExtraPlayerController *)Actor;
      break;
    }
  }

  if (localController) {

    for (auto Actor : Actors) {
      if (isObjectInvalid(Actor))
        continue;

      if (Actor->IsA(ASTExtraPlayerCharacter::StaticClass())) {
        if (((ASTExtraPlayerCharacter *)Actor)->PlayerKey ==
            localController->PlayerKey) {
          localPlayer = (ASTExtraPlayerCharacter *)Actor;
          break;
        }
      }
    }

    if (localPlayer) {
      if (localPlayer->PartHitComponent) {
        auto ConfigCollisionDistSqAngles =
            localPlayer->PartHitComponent->ConfigCollisionDistSqAngles;
        for (int j = 0; j < ConfigCollisionDistSqAngles.Num(); j++) {
          ConfigCollisionDistSqAngles[j].Angle = 90.0f;
        }
        localPlayer->PartHitComponent->ConfigCollisionDistSqAngles =
            ConfigCollisionDistSqAngles;
      }

      if (Config.AimBot.Enable) {
        EnhancedAimbot();
      }

      static std::unordered_map<uintptr_t, float> smoothHPValues;
      static std::unordered_map<std::string, float> grenadeTimers;
      static std::unordered_map<std::string, bool> grenadeVisible;
      if (smoothHPValues.empty()) { smoothHPValues.reserve(64); }
      if (grenadeTimers.empty()) { grenadeTimers.reserve(32); grenadeVisible.reserve(32); }

      for (int i = 0; i < Actors.size(); i++) {
        auto Actor = Actors[i];
        if (isObjectInvalid(Actor))
          continue;
        if (Actor->IsA(ASTExtraPlayerCharacter::StaticClass())) {
          int SCOLOR;
          int SCOLO;
          SCOLOR = IM_COL32(0, 255, 0, 255);
          SCOLO = IM_COL32(255, 100, 0, 100);
          auto Player = (ASTExtraPlayerCharacter *)Actor;
          if (!Player) continue;
          if (!localController->LineOfSightTo(Player, {0, 0, 0}, true)) {
            SCOLOR = IM_COL32(255, 0, 0, 255);
            SCOLO = IM_COL32(0, 0, 0, 100);
          }

          long PlayerBoxClrCf = IM_COL32(255, 255, 255, 255);
          long PlayerBoxClrCfline = IM_COL32(255, 0, 0, 255);
          long PlayerBoxClrCfbx = IM_COL32(255, 0, 0, 255);

          float Distance = localPlayer->GetDistanceTo(Player) / 100.0f;
          if (Distance > 500.0f)
            continue;

          if (Player->PlayerKey == localController->PlayerKey)
            continue;

          if (Player->TeamID == localController->TeamID)
            continue;

          if (Player->bDead)
            continue;

          if (Player->bEnsure) {
            totalBots++;
          } else {
            totalEnemies++;
          }

          bool isVisible = localController->LineOfSightTo(Player, {0, 0, 0}, true);

          if (Config.ESP.HideBot && Player->bEnsure)
            continue;

          if (Config.ESP.Alert) {
            bool shit = false;
            FVector MyPosition, EnemyPosition;
            ASTExtraVehicleBase *CurrentVehiclea = Player->CurrentVehicle;
            if (CurrentVehiclea) {
              MyPosition = CurrentVehiclea->RootComponent->RelativeLocation;
            } else {
              MyPosition = Player->RootComponent->RelativeLocation;
            }
            ASTExtraVehicleBase *CurrentVehicle = localPlayer->CurrentVehicle;
            if (CurrentVehicle) {
              EnemyPosition = CurrentVehicle->RootComponent->RelativeLocation;
            } else {
              EnemyPosition = localPlayer->RootComponent->RelativeLocation;
            }
            FVector EntityPos =
                WorldToRadar(localController->PlayerCameraManager->CameraCache
                        .POV.Rotation.Yaw,
                        MyPosition, EnemyPosition, NULL, NULL,
                        Vector3(glWidth, glHeight, 0), shit);
            FVector angle = FVector();
            Vector3 forward =
                Vector3((float)(glWidth / 2) - EntityPos.X,
                        (float)(glHeight / 2) - EntityPos.Y, 0.0f);
            VectorAnglesRadar(forward, angle);
            const auto angle_yaw_rad = DEG2RAD(angle.Y + 180.f);
            const auto new_point_x =
                (glWidth / 2) + (55) / 2 * 8 * cosf(angle_yaw_rad);
            const auto new_point_y =
                (glHeight / 2) + (55) / 2 * 8 * sinf(angle_yaw_rad);
            std::array<Vector3, 3> points{
                Vector3(new_point_x - ((90) / 4 + 3.5f) / 2,
                        new_point_y - ((55) / 4 + 3.5f) / 2, 0.f),
                Vector3(new_point_x + ((90) / 4 + 3.5f) / 4, new_point_y, 0.f),
                Vector3(new_point_x - ((90) / 4 + 3.5f) / 2,
                        new_point_y + ((55) / 4 + 3.5f) / 2, 0.f)};
            RotateTriangle(points, angle.Y + 180.f);
            if (Player->bEnsure) {
              draw->AddTriangle(ImVec2(points.at(0).X, points.at(0).Y),
                        ImVec2(points.at(1).X, points.at(1).Y),
                        ImVec2(points.at(2).X, points.at(2).Y),
                        IM_COL32(0, 255, 0, 255), 1.5f);
              draw->AddTriangleFilled(ImVec2(points.at(0).X, points.at(0).Y),
                        ImVec2(points.at(1).X, points.at(1).Y),
                        ImVec2(points.at(2).X, points.at(2).Y),
                        IM_COL32(0, 255, 0, 255));
            } else {
              draw->AddTriangle(ImVec2(points.at(0).X, points.at(0).Y),
                        ImVec2(points.at(1).X, points.at(1).Y),
                        ImVec2(points.at(2).X, points.at(2).Y),
                        IM_COL32(226, 8, 255, 255), 1.5f);
              draw->AddTriangleFilled(ImVec2(points.at(0).X, points.at(0).Y),
                        ImVec2(points.at(1).X, points.at(1).Y),
                        ImVec2(points.at(2).X, points.at(2).Y),
                        IM_COL32(226, 8, 255, 255));
            }
          }

          auto HeadPos = Player->GetBonePos("Head", {});
          ImVec2 headPosSC;
          auto RootPos  = Player->GetBonePos("Root", {});
          ImVec2 RootPosSC;
          auto pelvis = Player->GetBonePos("pelvis", {});
          ImVec2 pelvisPoSC;

          FVector upper_r={}, lowerarm_r={}, hand_r={}, upper_l={}, lowerarm_l={}, hand_l={};
          FVector thigh_l={}, calf_l={}, foot_l={}, thigh_r={}, calf_r={}, foot_r={};
          FVector neck_01={}, spine_01={}, spine_02={}, spine_03={};
          ImVec2 upper_rPoSC={}, lowerarm_rPoSC={}, hand_rPoSC={};
          ImVec2 upper_lPoSC={}, lowerarm_lSC={}, hand_lPoSC={};
          ImVec2 thigh_lPoSC={}, calf_lPoSC={}, foot_lPoSC={};
          ImVec2 thigh_rPoSC={}, calf_rPoSC={}, foot_rPoSC={};
          ImVec2 neck_01PoSC={}, spine_01PoSC={}, spine_02PoSC={}, spine_03PoSC={};
          bool _skelBonesOk = false;
          if (Config.ESP.Skeleton) {
            upper_r    = Player->GetBonePos(([&](){static char _b[11]={'u','p','p','e','r','a','r','m','_','r',0};return (const char*)_b;})(), {});
            lowerarm_r = Player->GetBonePos(([&](){static char _b[11]={'l','o','w','e','r','a','r','m','_','r',0};return (const char*)_b;})(), {});
            hand_r     = Player->GetBonePos(([&](){static char _b[7]={'h','a','n','d','_','r',0};return (const char*)_b;})(), {});
            upper_l    = Player->GetBonePos(([&](){static char _b[11]={'u','p','p','e','r','a','r','m','_','l',0};return (const char*)_b;})(), {});
            lowerarm_l = Player->GetBonePos(([&](){static char _b[11]={'l','o','w','e','r','a','r','m','_','l',0};return (const char*)_b;})(), {});
            hand_l     = Player->GetBonePos(([&](){static char _b[7]={'h','a','n','d','_','l',0};return (const char*)_b;})(), {});
            thigh_l    = Player->GetBonePos(([&](){static char _b[8]={'t','h','i','g','h','_','l',0};return (const char*)_b;})(), {});
            calf_l     = Player->GetBonePos(([&](){static char _b[7]={'c','a','l','f','_','l',0};return (const char*)_b;})(), {});
            foot_l     = Player->GetBonePos(([&](){static char _b[7]={'f','o','o','t','_','l',0};return (const char*)_b;})(), {});
            thigh_r    = Player->GetBonePos(([&](){static char _b[8]={'t','h','i','g','h','_','r',0};return (const char*)_b;})(), {});
            calf_r     = Player->GetBonePos(([&](){static char _b[7]={'c','a','l','f','_','r',0};return (const char*)_b;})(), {});
            foot_r     = Player->GetBonePos(([&](){static char _b[7]={'f','o','o','t','_','r',0};return (const char*)_b;})(), {});
            neck_01    = Player->GetBonePos(([&](){static char _b[8]={'n','e','c','k','_','0','1',0};return (const char*)_b;})(), {});
            spine_01   = Player->GetBonePos(([&](){static char _b[9]={'s','p','i','n','e','_','0','1',0};return (const char*)_b;})(), {});
            spine_02   = Player->GetBonePos(([&](){static char _b[9]={'s','p','i','n','e','_','0','2',0};return (const char*)_b;})(), {});
            spine_03   = Player->GetBonePos(([&](){static char _b[9]={'s','p','i','n','e','_','0','3',0};return (const char*)_b;})(), {});
            _skelBonesOk = W2S(upper_r,(FVector2D*)&upper_rPoSC) && W2S(upper_l,(FVector2D*)&upper_lPoSC) &&
                        W2S(lowerarm_r,(FVector2D*)&lowerarm_rPoSC) && W2S(hand_r,(FVector2D*)&hand_rPoSC) &&
                        W2S(lowerarm_l,(FVector2D*)&lowerarm_lSC) && W2S(hand_l,(FVector2D*)&hand_lPoSC) &&
                        W2S(thigh_l,(FVector2D*)&thigh_lPoSC) && W2S(calf_l,(FVector2D*)&calf_lPoSC) &&
                        W2S(foot_l,(FVector2D*)&foot_lPoSC) && W2S(thigh_r,(FVector2D*)&thigh_rPoSC) &&
                        W2S(calf_r,(FVector2D*)&calf_rPoSC) && W2S(foot_r,(FVector2D*)&foot_rPoSC) &&
                        W2S(neck_01,(FVector2D*)&neck_01PoSC) && W2S(spine_01,(FVector2D*)&spine_01PoSC) &&
                        W2S(spine_02,(FVector2D*)&spine_02PoSC) && W2S(spine_03,(FVector2D*)&spine_03PoSC);
          }
          if (W2S(HeadPos,  (FVector2D *)&headPosSC) &&
              W2S(pelvis,   (FVector2D *)&pelvisPoSC) &&
              W2S(RootPos,  (FVector2D *)&RootPosSC)) {

            if (Config.ESP.Line) {
              ImVec2 screenTopCenter = ImVec2((float)glWidth / 2, 0.0f);
              ImVec2 headPos = ImVec2(headPosSC.x, headPosSC.y);

              if (isVisible) {
                DrawLineWithOutline(draw, screenTopCenter, headPos,
                        IM_COL32(0, 255, 0, 255),
                        IM_COL32(0, 0, 0, 255), 0.1f, 1.0f);
              } else {
                DrawLineWithOutline(draw, screenTopCenter, headPos,
                        IM_COL32(255, 255, 0, 255),
                        IM_COL32(0, 0, 0, 255), 0.1f, 1.0f);
              }
            }

            if (Config.ESP.Skeleton && _skelBonesOk) {
              long Cneck = isVisible ? IM_COL32(0, 255, 0, 255) : IM_COL32(255, 255, 0, 255);
              long Cuparmr = isVisible ? IM_COL32(0, 255, 0, 255) : IM_COL32(255, 255, 0, 255);
              long Cuparml = isVisible ? IM_COL32(0, 255, 0, 255) : IM_COL32(255, 255, 0, 255);
              long Clowarmr = isVisible ? IM_COL32(0, 255, 0, 255) : IM_COL32(255, 255, 0, 255);
              long Clowarml = isVisible ? IM_COL32(0, 255, 0, 255) : IM_COL32(255, 255, 0, 255);
              long Cspine3 = isVisible ? IM_COL32(0, 255, 0, 255) : IM_COL32(255, 255, 0, 255);
              long Cspine2 = isVisible ? IM_COL32(0, 255, 0, 255) : IM_COL32(255, 255, 0, 255);
              long Cspine1 = isVisible ? IM_COL32(0, 255, 0, 255) : IM_COL32(255, 255, 0, 255);
              long Cpelvis = isVisible ? IM_COL32(0, 255, 0, 255) : IM_COL32(255, 255, 0, 255);
              long Cthighl = isVisible ? IM_COL32(0, 255, 0, 255) : IM_COL32(255, 255, 0, 255);
              long Cthighr = isVisible ? IM_COL32(0, 255, 0, 255) : IM_COL32(255, 255, 0, 255);
              long Ccalfl = isVisible ? IM_COL32(0, 255, 0, 255) : IM_COL32(255, 255, 0, 255);
              long Ccalfr = isVisible ? IM_COL32(0, 255, 0, 255) : IM_COL32(255, 255, 0, 255);

              float boxWidth = 7.f - Distance * 0.03;
              draw->AddLine({upper_rPoSC.x, upper_rPoSC.y}, neck_01PoSC, Cneck,
                        1.2f);
              draw->AddLine({upper_lPoSC.x, upper_lPoSC.y}, neck_01PoSC, Cneck,
                        1.2f);
              draw->AddLine({upper_rPoSC.x, upper_rPoSC.y}, lowerarm_rPoSC,
                        Cuparmr, 1.2f);
              draw->AddLine({lowerarm_rPoSC.x, lowerarm_rPoSC.y}, hand_rPoSC,
                        Clowarmr, 1.2f);
              draw->AddLine({upper_lPoSC.x, upper_lPoSC.y}, lowerarm_lSC,
                        Cuparml, 1.2f);
              draw->AddLine({lowerarm_lSC.x, lowerarm_lSC.y}, hand_lPoSC,
                        Clowarml, 1.2f);
              draw->AddLine({thigh_rPoSC.x, thigh_rPoSC.y}, thigh_lPoSC,
                        Cthighl, 1.2f);
              draw->AddLine({thigh_lPoSC.x, thigh_lPoSC.y}, calf_lPoSC, Cthighl,
                        1.2f);
              draw->AddLine({calf_lPoSC.x, calf_lPoSC.y}, foot_lPoSC, Ccalfl,
                        1.2f);
              draw->AddLine({thigh_rPoSC.x, thigh_rPoSC.y}, calf_rPoSC, Cthighr,
                        1.2f);
              draw->AddLine({calf_rPoSC.x, calf_rPoSC.y}, foot_rPoSC, Ccalfr,
                        1.2f);
              draw->AddLine({neck_01PoSC.x, neck_01PoSC.y}, spine_03PoSC,
                        Cspine3, 1.2f);
              draw->AddLine({spine_03PoSC.x, spine_03PoSC.y}, spine_02PoSC,
                        Cspine2, 1.2f);
              draw->AddLine({spine_02PoSC.x, spine_02PoSC.y}, spine_01PoSC,
                        Cspine1, 1.2f);
              draw->AddLine({spine_01PoSC.x, spine_01PoSC.y}, pelvisPoSC,
                        Cpelvis, 1.2f);
              draw->AddLine({neck_01PoSC.x, neck_01PoSC.y}, headPosSC, Cneck,
                        1.2f);
            }

            if (Config.ESP.Box) {
              float boxHeight = abs(headPosSC.y - RootPosSC.y);
              float boxWidth = boxHeight * 0.67f;
              ImVec2 vStart = {headPosSC.x - (boxWidth / 2), headPosSC.y};
              ImVec2 vEnd = {vStart.x + boxWidth, vStart.y + boxHeight};

              ImU32 boxColor = isVisible ? IM_COL32(0, 255, 0, 255) : IM_COL32(255, 255, 0, 255);

              DrawRectWithOutline(draw, vStart, vEnd, boxColor,
                        IM_COL32(0, 0, 0, 255), 1.0f, 0.0f, 240);
            }

            if (Config.ESP.Health) {
              int CurHP = (int)std::max(
                  0, std::min((int)Player->Health, (int)Player->HealthMax));
              int MaxHP = (int)Player->HealthMax;

              ImU32 color_red = ImColor(255, 25, 25);
              ImU32 color_orange = ImColor(255, 180, 0);
              ImU32 color_green = ImColor(50, 230, 50);
              ImU32 current_color = color_green;

              float health = Player->Health;
              if (health <= 50.0f) {
                current_color = color_orange;
              }
              if (health <= 25.0f) {
                current_color = color_red;
              }

              float boxHeight =
                  std::min(abs(headPosSC.y - RootPosSC.y), 150.0f);
              boxHeight = std::max(boxHeight, 30.0f);
              float boxWidth = 5.0f;

              bool isVisibleHealth = true;
              if (headPosSC.y < 0 || headPosSC.y > glHeight ||
                  RootPosSC.y < 0 || RootPosSC.y > glHeight) {
                isVisibleHealth = false;
              }

              ImVec2 vStart = {headPosSC.x + 15.0f, headPosSC.y};
              ImVec2 vEnd = {vStart.x + boxWidth, vStart.y + boxHeight};

              auto itHP = smoothHPValues.find((uintptr_t)Player);
              if (itHP == smoothHPValues.end()) {
                smoothHPValues.emplace((uintptr_t)Player, (float)CurHP);
                itHP = smoothHPValues.find((uintptr_t)Player);
              }
              float &smoothHP = itHP->second;
              smoothHP = lerp(smoothHP, (float)CurHP, 0.1f);

              if (!isVisibleHealth) {
                vStart.y =
                    std::clamp(vStart.y, 0.0f, (float)glHeight - boxHeight);
                vEnd.y = vStart.y + boxHeight;
                smoothHP = MaxHP;
              }

              float fillHeight = boxHeight * (smoothHP / MaxHP);
              ImVec2 vFilledStart = {vStart.x,
                        vStart.y + (boxHeight - fillHeight)};
              ImVec2 vFilledEnd = {vEnd.x, vEnd.y};

              if (isVisibleHealth || smoothHP < MaxHP) {
                draw->AddRectFilled(vFilledStart, vFilledEnd, current_color);
                draw->AddRect(vStart, vEnd, ImColor(0, 0, 0, 200), 0.0f, 0,
                        0.5f);
              }
            }

            if (Config.ESP.Distance) {
              std::string s = std::to_string((int)Distance) + "m";
              ImVec2 textSize = ImGui::CalcTextSize(s.c_str());

              ImVec2 textPos = {RootPosSC.x - (textSize.x / 2.0f),
                        RootPosSC.y + 5.0f};

              DrawOutlinedText(draw, s.c_str(), textPos,
                        IM_COL32(255, 255, 255, 255),
                        IM_COL32(0, 0, 0, 255), (float)density / 30.0f);
            }

            if (Config.ESP.Name) {
              float boxWidth = density / 1.6f;
              boxWidth -=
                  std::min(((boxWidth / 2) / 00.0f) * Distance, boxWidth / 2);
              float boxHeight = boxWidth * 0.15f;

              std::string s = Player->bEnsure ? "      Bot / Ai"
                        : Player->PlayerName.ToString();
              ImVec2 textPos = {headPosSC.x - (boxWidth / 3.5f),
                        headPosSC.y - (boxHeight * 1.0f)};

              DrawOutlinedText(draw, s.c_str(), textPos,
                        IM_COL32(255, 255, 255, 255),
                        IM_COL32(0, 0, 0, 255), (float)density / 30.0f);
            }

            if (Config.ESP.Airdrop) {
              if (Actors[i]->IsA(AAirDropBoxActor::StaticClass())) {
                auto DropBox = (AAirDropBoxActor *)Actors[i];
                auto RootComponent = DropBox->RootComponent;
                if (!RootComponent)
                  continue;
                float Distance = DropBox->GetDistanceTo(localPlayer) / 100.f;
                FVector2D DropBoxPos;
                if (W2S(DropBox->K2_GetActorLocation(), &DropBoxPos)) {
                  char _adBuf[32];
                  snprintf(_adBuf, sizeof(_adBuf), "AirDrop [%dM]", (int)Distance);
                  draw->AddText(NULL, ((float)density / 25.0f),
                        {DropBoxPos.X, DropBoxPos.Y},
                        IM_COL32(255, 255, 255, 255), _adBuf);
                }
              }
            }
          }
        }


        if (Config.ESP.Grenades) {
          if (Actor->IsA(ASTExtraGrenadeBase::StaticClass())) {
            auto Grenade = (ASTExtraGrenadeBase *)Actor;
            auto RootComponent = Actor->RootComponent;
            if (!RootComponent)
              continue;
            float Distance = Grenade->GetDistanceTo(localPlayer) / 100.f;
            FVector2D grenadePos;
            if (W2S(Grenade->K2_GetActorLocation(), &grenadePos)) {
              const std::string _gname = Grenade->GetName();
              const char* grenadeType = nullptr;
              if      (_gname.find("Shoulei") != std::string::npos) grenadeType = "Grenade";
              else if (_gname.find("Burn")    != std::string::npos) grenadeType = "Molotov";
              else if (_gname.find("Stun")    != std::string::npos) grenadeType = "Stun";
              else if (_gname.find("Smoke")   != std::string::npos) grenadeType = "Smoke";
              else continue;
              std::string grenadeID =
                  std::to_string(reinterpret_cast<uintptr_t>(Grenade));
              float offsetX = -30.0f;
              float offsetY = -30.0f;
              char _gText[64];
              snprintf(_gText, sizeof(_gText), "%s [%dM]", grenadeType, (int)Distance);
              const char* text = _gText;
              Outlined_Text(15.0f, grenadePos.X + offsetX,
                        grenadePos.Y + offsetY, ImVec4(255, 255, 255, 255),
                        text);
              auto itTimer = grenadeTimers.find(grenadeID);
              if (itTimer == grenadeTimers.end()) {
                grenadeTimers.emplace(grenadeID, ImGui::GetTime());
                grenadeVisible.emplace(grenadeID, true);
                itTimer = grenadeTimers.find(grenadeID);
              }
              float timer = itTimer->second;
              float elapsed = ImGui::GetTime() - timer;
              float fillAmount = elapsed / 7.0f;
              if (fillAmount > 1.0f) {
                fillAmount = 1.0f;
                grenadeVisible[grenadeID] = false;
              }
              if (grenadeVisible[grenadeID]) {
                ImVec2 pos = ImVec2(grenadePos.X, grenadePos.Y);
                float radius = 13.0f;
                ImU32 yellow = IM_COL32(255, 255, 255, 255);
                ImU32 red = IM_COL32(0, 0, 0, 255);
                draw->AddCircleFilled(pos, radius, yellow, 16);
                draw->PathArcTo(pos, radius - 3, -IM_PI / 2,
                        (-IM_PI / 2) + (IM_PI * 2 * fillAmount), 32);
                draw->PathLineTo(pos);
                draw->PathFillConvex(red);
              }
            }
          }
        }

        if (Config.ESP.Dogs) {
          if (Actor->IsA(UObjectCountWatcherComponent::StaticClass())) {
            auto Grenade = (UObjectCountWatcherComponent *)Actor;
            auto RootComponent = Actor->RootComponent;
            if (!RootComponent)
              continue;
            float dist = Actor->GetDistanceTo(localPlayer) / 100.f;
            if (dist > 200.0f)
              continue;
            FVector2D grenadePos;
            std::string classname = Grenade->GetName();
            if (classname.find("BPPawn_Library_C") != std::string::npos) {
              std::string s = "LIBRARIAN";
              s += "[";
              s += std::to_string((int)dist);
              s += "M]";
              draw->AddText(NULL, ((float)density / 20.0f),
                        {grenadePos.X, grenadePos.Y},
                        IM_COL32(255, 255, 255, 255), s.c_str());
            }
            if (classname.find("BPPAWn_HungerH_C") != std::string::npos) {
              std::string s = "HUNGER";
              s += "[";
              s += std::to_string((int)dist);
              s += "M]";
              draw->AddText(NULL, ((float)density / 20.0f),
                        {grenadePos.X, grenadePos.Y},
                        IM_COL32(255, 255, 255, 255), s.c_str());
            }
            if (classname.find("BPPawn_HungerB_C") != std::string::npos) {
              std::string s = "HUNGER 2";
              s += "[";
              s += std::to_string((int)dist);
              s += "M]";
              draw->AddText(NULL, ((float)density / 20.0f),
                        {grenadePos.X, grenadePos.Y},
                        IM_COL32(255, 255, 255, 255), s.c_str());
            }
            if (classname.find("BPPawn_Watcher_C") != std::string::npos) {
              std::string s = "WATCHER";
              s += "[";
              s += std::to_string((int)dist);
              s += "M]";
              draw->AddText(NULL, ((float)density / 20.0f),
                        {grenadePos.X, grenadePos.Y},
                        IM_COL32(255, 255, 255, 255), s.c_str());
            }
          }
        }

        if (Config.ESP.Lootbox) {
            if (Actors[i]->IsA(APickUpListWrapperActor::StaticClass())) {
                auto Pick = (APickUpListWrapperActor *)Actors[i];
                if (!Pick || !Pick->RootComponent) continue;
                auto PickUpDataList = Pick->GetDataList();
                if (PickUpDataList.Num() == 0) continue;
                float Distance = Pick->GetDistanceTo(localPlayer) / 100.f;
                FVector2D PickUpListsPos;
                if (W2S(Pick->K2_GetActorLocation(), &PickUpListsPos)) {
                    std::string s = "Loot-Box";
                    s += " - ";
                    s += std::to_string((int)Distance);
                    s += "M";
                    draw->AddText(NULL, ((float)density / 30.0f),
                        {PickUpListsPos.X, PickUpListsPos.Y},
                        IM_COL32(50, 255, 0, 255), s.c_str());

                    if (Distance <= 20.f && !items_data.empty()) {
                        auto mWidthScale = std::min(0.1f * Distance, 35.f);
                        auto boxWidth = 75.f - mWidthScale;
                        auto boxHeight = boxWidth * 0.120f;
                        Rect PlayerRect(PickUpListsPos.X - (boxWidth / 2),
                        PickUpListsPos.Y, boxWidth, boxHeight);
                        float posY = PickUpListsPos.Y - (PlayerRect.height * 2.0f);
                        for (int j = 0; j < PickUpDataList.Num(); j++) {
                        std::vector<std::string> s2;
                        std::string itm;
                        uint32_t tc = 0xFF000000;
                        for (const auto &category : items_data) {
                        if (!category.contains("Items")) continue;
                        for (const auto &item : category["Items"]) {
                        if (item.contains("itemId") && item["itemId"] == PickUpDataList[j].ID.TypeSpecificID) {
                        if (item.contains("itemTextColor"))
                        tc = strtoul(item["itemTextColor"].get<std::string>().c_str(), 0, 16);
                        if (item.contains("itemName"))
                        itm = item["itemName"].get<std::string>();
                        s2.push_back(itm);
                        break;
                        }
                        }
                        }
                        if (!s2.empty()) {
                        if (PickUpDataList[j].Count > 1) {
                        s2.push_back(" * ");
                        s2.push_back(std::to_string(PickUpDataList[j].Count));
                        }
                        std::string s3;
                        for (auto &s4 : s2) {
                        s3 += s4;
                        }
                        draw->AddText(NULL, ((float)density / 30.0f),
                        {PickUpListsPos.X, posY}, tc, s3.c_str());
                        posY -= PlayerRect.height * 2.00f;
                        }
                        }
                    }
                }
            }
        }

        if (Config.ESP.Vehicle) {
          if (Actors[i]->IsA(ASTExtraVehicleBase::StaticClass())) {
            auto Vehicle = (ASTExtraVehicleBase *)Actors[i];

            if (!Vehicle->Mesh)
              continue;

            float Distance = Vehicle->GetDistanceTo(localPlayer) / 100.f;

            FVector2D vehiclePos;
            if (W2S(Vehicle->K2_GetActorLocation(), &vehiclePos)) {
              std::string s = GetVehicleName(Vehicle);
              s += " [";
              s += std::to_string((int)Distance);
              s += "m]";

              DrawOutlinedText(draw, s.c_str(),
                        {vehiclePos.X - 10, vehiclePos.Y - 10},
                        IM_COL32(255, 255, 000, 255),
                        IM_COL32(0, 0, 0, 255), (float)density / 30.0f);
            }
          }
        }

        if (Actors[i]->IsA(APickUpWrapperActor::StaticClass())) {
            auto PickUp = (APickUpWrapperActor *)Actors[i];
            if (!PickUp || !PickUp->RootComponent) continue;
            if (Items.find(PickUp->DefineID.TypeSpecificID) != Items.end() && Items[PickUp->DefineID.TypeSpecificID]) {
                float Distance = PickUp->GetDistanceTo(localPlayer) / 100.f;
                FVector2D itemPos;
                if (W2S(PickUp->K2_GetActorLocation(), &itemPos)) {
                    std::string s;
                    if (!items_data.empty()) {
                        for (const auto &category : items_data) {
                        if (!category.contains("Items")) continue;
                        for (const auto &item : category["Items"]) {
                        if (item.contains("itemId") && item["itemId"] == PickUp->DefineID.TypeSpecificID) {
                        if (item.contains("itemName"))
                        s = item["itemName"].get<std::string>();
                        break;
                        }
                        }
                        }
                    }
                    s += " - ";
                    s += std::to_string((int)Distance);
                    s += "m";
                    float* color = ItemColors[PickUp->DefineID.TypeSpecificID];
                    ImU32 col = color ? ToColor(color) : IM_COL32(255,255,255,255);
                    draw->AddText(NULL, ((float)density / 27.0f), {itemPos.X, itemPos.Y}, col, s.c_str());
                }
            }
        }
      }
    }

    if (localPlayer) {
      ImFont *font  = ImGui::GetIO().Fonts->Fonts[0];
      float cx      = (float)glWidth * 0.5f;
      float numSize = (float)density / 20.0f;
      float scaleN  = numSize / font->FontSize;

      std::string sBot = std::to_string(totalBots);
      std::string sPlr = std::to_string(totalEnemies);

      float wBot = ImGui::CalcTextSize(sBot.c_str()).x * scaleN;
      float wPlr = ImGui::CalcTextSize(sPlr.c_str()).x * scaleN;
      float hNum = ImGui::CalcTextSize("0").y           * scaleN;

      float hpad  = numSize * 1.4f;
      float vpad  = numSize * 0.55f;
      float capsH = hNum + vpad * 2.0f;
      float capsW = wBot + wPlr + hpad * 4.0f + 1.5f;
      float rnd   = capsH * 0.5f;
      float topY  = capsH * 0.3f;

      float x0 = cx - capsW * 0.5f;
      float y0 = topY;
      float x1 = cx + capsW * 0.5f;
      float y1 = topY + capsH;

      draw->AddRectFilled({x0, y0}, {x1, y1}, IM_COL32(70, 70, 70, 220), rnd);

      draw->AddRectFilled({cx - 1.0f, y0 + vpad * 0.8f},
                        {cx + 1.0f, y1 - vpad * 0.8f},
                        IM_COL32(0, 0, 0, 180));

      float numY = y0 + vpad;

      float xBot = cx - hpad * 0.6f - wBot;
      draw->AddText(font, numSize, {xBot, numY}, IM_COL32(0, 255, 0, 255), sBot.c_str());

      float xPlr = cx + hpad * 0.6f;
      draw->AddText(font, numSize, {xPlr, numY}, IM_COL32(255, 40, 40, 255), sPlr.c_str());
    }

    g_LocalPlayer = localPlayer;
    g_LocalController = localController;
  }
}

std::string getClipboardText() {
  if (!g_App)
    return "";

  auto activity = g_App->activity;
  if (!activity)
    return "";

  auto vm = activity->vm;
  if (!vm)
    return "";

  auto object = activity->clazz;
  if (!object)
    return "";

  std::string result;

  JNIEnv *env;
  vm->AttachCurrentThread(&env, 0);
  {
    auto ContextClass = env->FindClass("android/content/Context");
    auto getSystemServiceMethod =
        env->GetMethodID(ContextClass, "getSystemService",
                        "(Ljava/lang/String;)Ljava/lang/Object;");

    auto str = env->NewStringUTF("clipboard");
    auto clipboardManager =
        env->CallObjectMethod(object, getSystemServiceMethod, str);
    env->DeleteLocalRef(str);

    auto ClipboardManagerClass =
        env->FindClass("android/content/ClipboardManager");
    auto getText = env->GetMethodID(ClipboardManagerClass, "getText",
                        "()Ljava/lang/CharSequence;");

    auto CharSequenceClass = env->FindClass("java/lang/CharSequence");
    auto toStringMethod =
        env->GetMethodID(CharSequenceClass, "toString", "()Ljava/lang/String;");

    auto text = env->CallObjectMethod(clipboardManager, getText);
    if (text) {
      str = (jstring)env->CallObjectMethod(text, toStringMethod);
      result = env->GetStringUTFChars(str, 0);
      env->DeleteLocalRef(str);
      env->DeleteLocalRef(text);
    }

    env->DeleteLocalRef(CharSequenceClass);
    env->DeleteLocalRef(ClipboardManagerClass);
    env->DeleteLocalRef(clipboardManager);
    env->DeleteLocalRef(ContextClass);
  }
  vm->DetachCurrentThread();

  return result;
}

const char *GetAndroidID(JNIEnv *env, jobject context) {
  jclass contextClass =
      env->FindClass(/*android/content/Context*/ StrEnc(
                        "`L+&0^[S+-:J^$,r9q92(as",
                        "\x01\x22\x4F\x54\x5F\x37\x3F\x7C\x48\x42\x54\x3E\x3B"
                        "\x4A\x58\x5D\x7A\x1E\x57\x46\x4D\x19\x07",
                        23)
                        .c_str());
  jmethodID getContentResolverMethod = env->GetMethodID(
      contextClass, /*getContentResolver*/
      StrEnc("E8X\\7r7ys_Q%JS+L+~",
            "\x22\x5D\x2C\x1F\x58\x1C\x43\x1C\x1D\x2B\x03\x40"
            "\x39\x3C\x47\x3A\x4E\x0C",
            18)
          .c_str(),
      /*()Landroid/content/ContentResolver;*/
      StrEnc("8^QKmj< }5D:9q7f.BXkef]A*GYLNg}B!/L",
            "\x10\x77\x1D\x2A\x03\x0E\x4E\x4F\x14\x51\x6B\x59\x56\x1F\x43\x03"
            "\x40\x36\x77\x28\x0A\x08\x29\x24\x44\x33\x0B\x29\x3D\x08\x11\x34"
            "\x44\x5D\x77",
            35)
          .c_str());
  jclass settingSecureClass = env->FindClass(
      /*android/provider/Settings$Secure*/ StrEnc(
          "T1yw^BCF^af&dB_@Raf}\\FS,zT~L(3Z\"",
          "\x35\x5F\x1D\x05\x31\x2B\x27\x69\x2E\x13\x09\x50\x0D\x26\x3A\x32\x7D"
          "\x32\x03\x09\x28\x2F\x3D\x4B\x09\x70\x2D\x29\x4B\x46\x28\x47",
          32)
          .c_str());
  jmethodID getStringMethod = env->GetStaticMethodID(
      settingSecureClass,
      /*getString*/
      StrEnc("e<F*J5c0Y", "\x02\x59\x32\x79\x3E\x47\x0A\x5E\x3E", 9).c_str(),
      /*(Landroid/content/ContentResolver;Ljava/lang/String;)Ljava/lang/String;*/
      StrEnc("$6*%R*!XO\"m18o,0S!*`uI$IW)l_/"
            "_knSdlRiO1T`2sH|Ouy__^}%Y)JsQ:-\"(2_^-$i{?H",
            "\x0C\x7A\x4B\x4B\x36\x58\x4E\x31\x2B\x0D\x0E\x5E\x56\x1B\x49\x5E"
            "\x27\x0E\x69\x0F\x1B\x3D\x41\x27\x23\x7B\x09\x2C\x40\x33\x1D\x0B"
            "\x21\x5F\x20\x38\x08\x39\x50\x7B\x0C\x53\x1D\x2F\x53\x1C\x01\x0B"
            "\x36\x31\x39\x46\x0C\x15\x43\x2B\x05\x30\x15\x41\x43\x46\x55\x70"
            "\x0D\x59\x56\x00\x15\x58\x73",
            71)
          .c_str());

  auto obj = env->CallObjectMethod(context, getContentResolverMethod);
  auto str = (jstring)env->CallStaticObjectMethod(
      settingSecureClass, getStringMethod, obj,
      env->NewStringUTF(
          /*android_id*/ StrEnc("ujHO)8OfOE",
                        "\x14\x04\x2C\x3D\x46\x51\x2B\x39\x26\x21", 10)
              .c_str()));
  return env->GetStringUTFChars(str, 0);
}

const char *GetDeviceModel(JNIEnv *env) {
  jclass buildClass = env->FindClass(
      /*android/os/Build*/ StrEnc(
          "m5I{GKGWBP-VOxkA",
          "\x0C\x5B\x2D\x09\x28\x22\x23\x78\x2D\x23\x02\x14\x3A\x11\x07\x25",
          16)
          .c_str());
  jfieldID modelId = env->GetStaticFieldID(
      buildClass, /*MODEL*/ StrEnc("|}[q:", "\x31\x32\x1F\x34\x76", 5).c_str(),
      /*Ljava/lang/String;*/
      StrEnc(".D:C:ETZ1O-Ib&^h.Y",
            "\x62\x2E\x5B\x35\x5B\x6A\x38\x3B\x5F\x28\x02\x1A\x16\x54\x37\x06"
            "\x49\x62",
            18)
          .c_str());

  auto str = (jstring)env->GetStaticObjectField(buildClass, modelId);
  return env->GetStringUTFChars(str, 0);
}

const char *GetDeviceBrand(JNIEnv *env) {
  jclass buildClass = env->FindClass(
      /*android/os/Build*/ StrEnc(
          "0iW=2^>0zTRB!B90",
          "\x51\x07\x33\x4F\x5D\x37\x5A\x1F\x15\x27\x7D\x00\x54\x2B\x55\x54",
          16)
          .c_str());
  jfieldID modelId = env->GetStaticFieldID(
      buildClass, /*BRAND*/ StrEnc("@{[FP", "\x02\x29\x1A\x08\x14", 5).c_str(),
      /*Ljava/lang/String;*/
      StrEnc(".D:C:ETZ1O-Ib&^h.Y",
            "\x62\x2E\x5B\x35\x5B\x6A\x38\x3B\x5F\x28\x02\x1A\x16\x54\x37\x06"
            "\x49\x62",
            18)
          .c_str());

  auto str = (jstring)env->GetStaticObjectField(buildClass, modelId);
  return env->GetStringUTFChars(str, 0);
}

const char *GetPackageName(JNIEnv *env, jobject context) {
  jclass contextClass =
      env->FindClass(/*android/content/Context*/ StrEnc(
                        "`L+&0^[S+-:J^$,r9q92(as",
                        "\x01\x22\x4F\x54\x5F\x37\x3F\x7C\x48\x42\x54\x3E\x3B"
                        "\x4A\x58\x5D\x7A\x1E\x57\x46\x4D\x19\x07",
                        23)
                        .c_str());
  jmethodID getPackageNameId = env->GetMethodID(
      contextClass,
      /*getPackageName*/
      StrEnc("YN4DaP)!{wRGN}",
            "\x3E\x2B\x40\x14\x00\x33\x42\x40\x1C\x12\x1C\x26\x23\x18", 14)
          .c_str(),
      /*()Ljava/lang/String;*/
      StrEnc("VnpibEspM(b]<s#[9cQD",
            "\x7E\x47\x3C\x03\x03\x33\x12\x5F\x21\x49\x0C\x3A\x13\x20\x57\x29"
            "\x50\x0D\x36\x7F",
            20)
          .c_str());

  auto str = (jstring)env->CallObjectMethod(context, getPackageNameId);
  return env->GetStringUTFChars(str, 0);
}

const char *GetDeviceUniqueIdentifier(JNIEnv *env, const char *uuid) {
  jclass uuidClass = env->FindClass(
      /*java/util/UUID*/ StrEnc(
          "B/TxJ=3BZ_]SFx",
          "\x28\x4E\x22\x19\x65\x48\x47\x2B\x36\x70\x08\x06\x0F\x3C", 14)
          .c_str());

  auto len = strlen(uuid);

  jbyteArray myJByteArray = env->NewByteArray(len);
  env->SetByteArrayRegion(myJByteArray, 0, len, (jbyte *)uuid);

  jmethodID nameUUIDFromBytesMethod = env->GetStaticMethodID(
      uuidClass, /*nameUUIDFromBytes*/
      StrEnc("P6LV|'0#A+zQmoat,",
            "\x3E\x57\x21\x33\x29\x72\x79\x67\x07\x59\x15\x3C\x2F"
            "\x16\x15\x11\x5F",
            17)
          .c_str(),
      /*([B)Ljava/util/UUID;*/
      StrEnc("sW[\"Q[W3,7@H.vT0) xB",
            "\x5B\x0C\x19\x0B\x1D\x31\x36\x45\x4D\x18\x35\x3C\x47\x1A\x7B\x65"
            "\x7C\x69\x3C\x79",
            20)
          .c_str());
  jmethodID toStringMethod = env->GetMethodID(
      uuidClass,
      /*toString*/
      StrEnc("2~5292eW", "\x46\x11\x66\x46\x4B\x5B\x0B\x30", 8).c_str(),
      /*()Ljava/lang/String;*/
      StrEnc("P$BMc' #j?<:myTh_*h0",
            "\x78\x0D\x0E\x27\x02\x51\x41\x0C\x06\x5E\x52\x5D\x42\x2A\x20\x1A"
            "\x36\x44\x0F\x0B",
            20)
          .c_str());

  auto obj = env->CallStaticObjectMethod(uuidClass, nameUUIDFromBytesMethod,
                        myJByteArray);
  auto str = (jstring)env->CallObjectMethod(obj, toStringMethod);
  return env->GetStringUTFChars(str, 0);
}

struct MemoryStruct {
  char *memory;
  size_t size;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb,
                        void *userp) {
  size_t realsize = size * nmemb;
  struct MemoryStruct *mem = (struct MemoryStruct *)userp;
  if (!mem) return 0;

  if (mem->size + realsize + 1 > 65536) return 0;

  char *ptr = (char *)realloc(mem->memory, mem->size + realsize + 1);
  if (!ptr) return 0;

  mem->memory = ptr;
  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;

  return realsize;
}

std::string Login(const char *user_key) {
  if (!g_App) return "Internal Error";
  auto activity = g_App->activity;
  if (!activity || !activity->vm || !activity->clazz) return "Internal Error";

  JNIEnv *env;
  activity->vm->AttachCurrentThread(&env, 0);
  std::string lux = user_key;
  lux += GetAndroidID(env, activity->clazz);
  lux += GetDeviceModel(env);
  lux += GetDeviceBrand(env);
  const char* _rawUUID = GetDeviceUniqueIdentifier(env, lux.c_str());
  std::string UUID = _rawUUID ? _rawUUID : "";
  activity->vm->DetachCurrentThread();

    struct MemoryStruct chunk{};
  chunk.memory = (char*)malloc(1);
  chunk.size   = 0;
  std::string errMsg;

  CURL *curl = curl_easy_init();
  if (!curl) { free(chunk.memory); return "curl init failed"; }

  std::string url = _DecXOR(_xor_url);

  char postData[512];
  snprintf(postData, sizeof(postData), "game=PUBG&user_key=%s&serial=%s",
          user_key, UUID.c_str());

  curl_easy_setopt(curl, CURLOPT_URL, _DecXOR(_xor_url).c_str());
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST,  "POST");
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS,     postData);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  WriteMemoryCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA,      (void*)&chunk);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  curl_easy_setopt(curl, CURLOPT_SSLVERSION,     CURL_SSLVERSION_TLSv1_2);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT,        15L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);

  struct curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

  CURLcode res = curl_easy_perform(curl);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK) {
    errMsg = curl_easy_strerror(res);
    if (chunk.memory) { memset(chunk.memory, 0, chunk.size); free(chunk.memory); }
    return errMsg;
  }

    try {
    json result = json::parse(chunk.memory);

    memset(chunk.memory, 0, chunk.size);
    free(chunk.memory);
    chunk.memory = nullptr;

    if (!result.contains("status") || result["status"] != true) {
      errMsg = result.contains("reason") ? result["reason"].get<std::string>() : "Auth failed";
      return errMsg;
    }

    auto &data  = result["data"];
    std::string token = data["token"].get<std::string>();

    std::string auth = "PUBG-";
    auth += user_key;
    auth += "-";
    auth += UUID;
    auth += "-";
    {
      std::string _sfx = StrEnc(
          ENCLKOVATE("ZD$_K NtaM8Fu=n0fFyO;!Ae<H)*Gy4%"),
          ENCLKOVATE("\x0C\x29\x1C\x13\x20\x17\x1B\x1E\x53\x07\x55"
                    "\x35\x1F\x7E\x3E\x66\x36\x10\x13\x3D\x77\x40"
                    "\x76\x1F\x5B\x2E\x51\x19\x32\x03\x0D\x60"), 32).c_str();
      auth += _sfx;
    }

    std::string expectedToken = Tools::CalcMD5(auth);

    if (token != expectedToken) {
      return "Invalid server response";
    }

    uint32_t dh = 0x811C9DC5u;
    for (char c : UUID) { dh ^= (uint8_t)c; dh *= 0x01000193u; }
    g_DeviceHash       = dh;
    g_DeviceHashMirror = ~dh;

    if (data.contains("expires_at")) {
      g_KeyExpiry = data["expires_at"].get<std::string>();
    } else {
      time_t _now = time(nullptr);
      struct tm* _tm = localtime(&_now);
      char _eb[64];
      strftime(_eb, sizeof(_eb), "%Y-%m-%d %H:%M:%S", _tm);
      g_KeyExpiry = _eb;
    }
    _SetValid(true);
    g_MenuOpen = true;
    LoadConfig();
    return "OK";

  } catch (...) {
    if (chunk.memory) { memset(chunk.memory, 0, chunk.size); free(chunk.memory); }
    return "Parse error";
  }
}


EGLBoolean (*orig_eglSwapBuffers)(EGLDisplay dpy, EGLSurface surface);
EGLBoolean _eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    eglQuerySurface(dpy, surface, EGL_WIDTH, &glWidth); eglQuerySurface(dpy, surface, EGL_HEIGHT, &glHeight);
    if (glWidth <= 0 || glHeight <= 0) return orig_eglSwapBuffers(dpy, surface);
    if (!g_App) return orig_eglSwapBuffers(dpy, surface);
    screenWidth = ANativeWindow_getWidth(g_App->window); screenHeight = ANativeWindow_getHeight(g_App->window); density = AConfiguration_getDensity(g_App->config);

  if (!initImGui) {

    ImGui::CreateContext();
    ImGuiStyle &current_style = ImGui::GetStyle();

    current_style.WindowRounding = 18.0f;
    current_style.WindowBorderSize = 1.5f;
    current_style.WindowPadding = ImVec2(18.0f, 18.0f);
    current_style.FrameRounding = 12.0f;
    current_style.FrameBorderSize = 1.0f;
    current_style.ChildRounding = 12.0f;
    current_style.ChildBorderSize = 1.0f;
    current_style.ScrollbarSize = 14.0f;
    current_style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.05f, 0.10f, 0.50f);
    current_style.Colors[ImGuiCol_Button] = ImVec4(0.25f, 0.40f, 0.85f, 0.90f);
    current_style.Colors[ImGuiCol_ButtonActive]  = ImVec4(0.10f, 0.25f, 0.70f, 1.00f);
    current_style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.35f, 0.55f, 1.00f, 1.00f);
    current_style.Colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.15f, 0.22f, 0.90f);
    current_style.Colors[ImGuiCol_Border] = ImColor(61, 61, 61, 255);
    current_style.Colors[ImGuiCol_ChildBg] = ImColor(33, 33, 33, 150);
    current_style.Colors[ImGuiCol_CheckMark] = ImColor(45, 173, 79, 255);
    current_style.Colors[ImGuiCol_SliderGrab] = ImColor(45, 173, 79, 255);
    current_style.Colors[ImGuiCol_SliderGrabActive] = ImColor(45, 173, 79, 255);

    ImGui_ImplAndroid_Init();
    ImGui_ImplOpenGL3_Init(ENCLKOVATE("#version 300 es"));
    ImGuiIO &io = ImGui::GetIO();

    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.IniFilename = NULL;

    static const ImWchar icons_ranges[] = {0xf000, 0xf3ff, 0};
    ImFontConfig icons_config;

    ImFontConfig CustomFont;
    CustomFont.FontDataOwnedByAtlas = false;
    icons_config.MergeMode = true;
    icons_config.PixelSnapH = true;
    icons_config.OversampleH = 2.5;
    icons_config.OversampleV = 2.5;
    ImFontConfig font_config;
    font_config.OversampleH = 1;
    font_config.OversampleV = 1;
    font_config.FontBuilderFlags = 1;

    ImFontConfig cfg;
    cfg.SizePixels = ((float)density / 20.0f);

    font_config.GlyphRanges = icons_ranges;
    io.Fonts->AddFontFromMemoryTTF((void *)Custom, sizeof Custom, 16.5f, NULL,
                        io.Fonts->GetGlyphRangesCyrillic());

    io.Fonts->AddFontFromMemoryCompressedTTF(font_awesome_data,
                        font_awesome_size, 25.0f,
                        &icons_config, icons_ranges);
    g_BgTex = LoadTextureFromMemory(BackgroundImg, sizeof(BackgroundImg));
    g_LogoTex = LoadTextureFromMemory(LogoImg, sizeof(LogoImg));

    for (auto &i : items_data) {
      for (auto &item : i["Items"]) {
        int r, g, b;
        sscanf(item["itemTextColor"].get<std::string>().c_str(),
              "#%02X%02X%02X", &r, &g, &b);
        ItemColors[item["itemId"].get<int>()] = CREATE_COLOR(r, g, b, 255);
      }
    }
    initImGui = true;
  }

  ImGuiIO &io = ImGui::GetIO();

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplAndroid_NewFrame(glWidth, glHeight);
  ImGui::NewFrame();

  _CheckValid();

  // ---------------- ИЗМЕНЕНИЕ 3: Сначала даем войти, проверка взлома только после логина! ----------------
  if (!_CV_IsValid()) {
    ImGuiWindowFlags loginFlags =
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove;

    ImGui::SetNextWindowSize(ImVec2(420.0f, 340.0f));
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                        ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##login_wnd", nullptr, loginFlags);
    ImGui::PopStyleVar(2);
    {
      ImDrawList *dlg = ImGui::GetWindowDrawList();

      ImVec2 wp = ImGui::GetWindowPos();
      ImVec2 ws = ImGui::GetWindowSize();
      float rounding = 20.0f;

      if (g_BgTex.ok) {
        dlg->PushClipRect(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), false);
        dlg->AddImageRounded((ImTextureID)(uintptr_t)g_BgTex.id, wp,
                        ImVec2(wp.x + ws.x, wp.y + ws.y), ImVec2(0, 0),
                        ImVec2(1, 1), IM_COL32(255, 255, 255, 255),
                        rounding, ImDrawFlags_RoundCornersAll);
        dlg->PopClipRect();
      }

      if (g_LogoTex.ok) {
        float lw = 70.0f, lh = 70.0f;
        float lx = wp.x + (ws.x - lw) * 0.5f;
        float ly = wp.y + 24.0f;
        dlg->AddImageRounded((ImTextureID)(uintptr_t)g_LogoTex.id,
                        ImVec2(lx, ly), ImVec2(lx + lw, ly + lh),
                        ImVec2(0, 0), ImVec2(1, 1),
                        IM_COL32(255, 255, 255, 255), 12.0f);
      }

      ImGui::SetCursorPosY(84.0f);

      const char *title =
          (current_lang == ENG) ? "Key Activation" : "Активация ключа";
      float tw = ImGui::CalcTextSize(title).x;
      ImGui::SetCursorPosX((ws.x - tw) * 0.5f);
      ImGui::TextUnformatted(title);

      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.0f);

      ImGui::SetNextItemWidth(ws.x - 40.0f);
      ImGui::SetCursorPosX(20.0f);
      const char *hint =
          (current_lang == ENG) ? "Enter key..." : "Введите ключ...";
      ImGui::InputTextWithHint("##key_input", hint, g_KeyBuf, sizeof(g_KeyBuf));

      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.0f);

      float btnW = (ws.x - 50.0f) * 0.5f;
      ImGui::SetCursorPosX(20.0f);

      {
        std::lock_guard<std::mutex> lk(g_LoginMutex);
        bool isLoading = (g_LoginState == LoginState::LOADING);

        if (isLoading)
          ImGui::BeginDisabled();

        const char *btnLabel =
            isLoading ? ((current_lang == ENG) ? "Loading..." : "Загрузка...")
                      : ((current_lang == ENG) ? "Login" : "Войти");

        if (ImGui::Button(btnLabel, ImVec2(btnW, 34.0f)) && !isLoading) {
          if (g_KeyBuf[0] != '\0') {
            if (!_LoginRateLimitCheck()) {
              std::lock_guard<std::mutex> lk2(g_LoginMutex);
              g_LoginError    = (current_lang == ENG)
                        ? "Too many attempts. Please wait."
                        : "Слишком много попыток. Подождите.";
              g_LoginState    = LoginState::ERROR_MSG;
              memset(g_KeyBuf, 0, sizeof(g_KeyBuf));
            } else {
              g_LoginState = LoginState::LOADING;
              if (g_LoginThread.joinable())
                g_LoginThread.join();
              std::string keyStr = g_KeyBuf;
              memset(g_KeyBuf, 0, sizeof(g_KeyBuf));
              g_LoginThread = std::thread([keyStr]() {
                std::string result = Login(keyStr.c_str());
                std::lock_guard<std::mutex> lk(g_LoginMutex);
                _CheckValid();
        bool ok = _CV_IsValid();
                _LoginRecordAttempt(ok);
                if (ok) {
                  SaveKey(keyStr);
                  g_LoginState = LoginState::SUCCESS;
                } else {
                  DeleteKeyFile();
                  g_LoginError = (current_lang == ENG)
                        ? "Invalid key or device."
                        : "Неверный ключ или устройство.";
                  g_LoginState = LoginState::ERROR_MSG;
                }
              });
            }
          }
        }
        if (isLoading)
          ImGui::EndDisabled();
      }

      ImGui::SameLine(0.0f, 10.0f);

      const char *pasteLabel = (current_lang == ENG) ? "Paste" : "Вставить";
      if (ImGui::Button(pasteLabel, ImVec2(btnW, 34.0f))) {
        std::string clip = getClipboardText();
        if (!clip.empty()) {
          strncpy(g_KeyBuf, clip.c_str(), sizeof(g_KeyBuf) - 1);
          g_KeyBuf[sizeof(g_KeyBuf) - 1] = '\0';
        }
      }


ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.0f);

const char *channelLabel = (current_lang == ENG) ? "Get Key" : "Получить ключ";
ImGui::SetCursorPosX(20.0f);
if (ImGui::Button(channelLabel, ImVec2(ws.x - 40.0f, 34.0f))) {
    OpenURL_Safe(ENCLKOVATE("https://t.me/UNFAILMOD"));
}

      {
        std::lock_guard<std::mutex> lk(g_LoginMutex);
        if (g_LoginState == LoginState::ERROR_MSG && !g_LoginError.empty()) {
          ImGui::SetCursorPosX(20.0f);
          ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 80, 80, 255));
          ImGui::TextWrapped("%s", g_LoginError.c_str());
          ImGui::PopStyleColor();
        }
      }
    }
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    return orig_eglSwapBuffers(dpy, surface);
  }

  // Если залогинились, рисуем ESP
  if (UE4) {
      DrawESP(ImGui::GetBackgroundDrawList(), glWidth, glHeight);
  }

  // Если после успешного входа (с ключом) мы обнаружили взлом в игре - тогда вылетает красный экран!
  if (g_CrackDetected) {
    _DrawErrorScreen(ImGui::GetBackgroundDrawList(), glWidth, glHeight);
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    return orig_eglSwapBuffers(dpy, surface);
  }

  ImGuiWindowFlags flags_main = ImGuiWindowFlags_NoResize |
                        ImGuiWindowFlags_NoTitleBar |
                        ImGuiWindowFlags_NoScrollbar;

  ImVec2 logo_size = ImVec2(200.0f, 100.0f);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 100.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));

  ImGui::SetNextWindowSize({140, 140});
  ImGui::SetNextWindowPos(ImVec2(50.0f, 20.0f), ImGuiCond_Once);
  ImGui::Begin("##iconbutton", nullptr,
              ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize);
  {
    ImTextureID texID = (ImTextureID)(intptr_t)g_LogoTex.id;
    ImVec2 wpos = ImGui::GetWindowPos();
    ImVec2 wsize = ImGui::GetWindowSize();

    ImDrawList *wdl = ImGui::GetWindowDrawList();
    float cx = wpos.x + 70.0f;
    float cy = wpos.y + 70.0f;
    float r  = 55.0f;

    wdl->AddCircleFilled(ImVec2(cx, cy), r, IM_COL32(0, 0, 0, 255), 64);

    wdl->AddImageRounded(texID,
                        ImVec2(cx - r, cy - r), ImVec2(cx + r, cy + r),
                        ImVec2(0, 0), ImVec2(1, 1),
                        IM_COL32(255, 255, 255, 255),
                        r, ImDrawFlags_RoundCornersAll);

    ImGui::SetCursorPos({0, 0});
    ImGui::InvisibleButton("##dragarea", wsize,
                        ImGuiButtonFlags_MouseButtonLeft);

    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0, 1.0f)) {
      ImVec2 delta = ImGui::GetIO().MouseDelta;
      ImGui::SetWindowPos(ImVec2(wpos.x + delta.x, wpos.y + delta.y));
    } else if (ImGui::IsItemClicked() && !ImGui::IsMouseDragging(0, 3.0f)) {
      g_MenuOpen = !g_MenuOpen;
    }
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(1);

  if (_CV_IsValid() && g_MenuOpen) {

    static int tabs = 0;
    {

    ImGui::SetNextWindowSize(ImVec2(656.0f, 386.0f));
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##salvador", nullptr, flags_main);
    ImGui::PopStyleVar(2);
    {
      ImVec2 pos = ImGui::GetWindowPos();
      ImVec2 size = ImGui::GetWindowSize();
      float window_height = size.y;
      float rounding = 20.0f;

      float scale = 0.32f;
      float logo_width = 262.0f * scale;
      float logo_height = 282.0f * scale;

      if (g_BgTex.ok) {
        ImDrawList *wdl = ImGui::GetWindowDrawList();
        wdl->PushClipRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), false);
        wdl->AddImageRounded((ImTextureID)(uintptr_t)g_BgTex.id, pos,
                        ImVec2(pos.x + size.x, pos.y + size.y),
                        ImVec2(0, 0), ImVec2(1, 1),
                        IM_COL32(255, 255, 255, 255), rounding,
                        ImDrawFlags_RoundCornersAll);
        wdl->PopClipRect();
      }

      float left_area_x = pos.x + 20.0f;
      float left_area_width = 197.0f;
      float logo_x = left_area_x + (left_area_width - logo_width) * 0.5f;
      float logo_y = pos.y + 20.0f;

      if (g_LogoTex.ok)
        ImGui::GetWindowDrawList()->AddImageRounded(
            (ImTextureID)(uintptr_t)g_LogoTex.id, ImVec2(logo_x, logo_y),
            ImVec2(logo_x + logo_width, logo_y + logo_height), ImVec2(0, 0),
            ImVec2(1, 1), IM_COL32(255, 255, 255, 255), 20.0f);

      const char *menu[][2] = {
      {"Home", "Главная"},
      {"Visuals", "Визуалы"},
      {"AimBot", "АимБот"},
      {"Memory", "Память"},
      {"ENGLISH", "РУССКИЙ"},
      {"ESP PLAYERS", "ЕСП ИГРОКОВ"},
      {"ESP WORLD", "ОБЩИЙ ЕСП"},
      {"MEMORY", "МЕМОРИ"},
      {"AIM-BOT", "АИМ-БОТ"},
      {"AIM-BOT SETTINGS", "НАСТРОЙКИ АИМ-БОТА"},
      {"HOME", "ГЛАВНАЯ"}
    };

      const char *esp_player[][2]{
          {"Lines", "Линии"},
          {"Name", "Имя"},
          {"Health", "Жизни"},
          {"TeamID", "Айди Команды"},
          {"Distance", "Дистанция"},
          {"Skeleton", "Скелет"},
          {"Box", "Коробка"},
          {"Alerts", "Стрелки"},
          {"Hide Bots", "Скрыть Ботов"}
      };

      const char *esp_world[][2]{
          {"Loot-Box", "Лут-Бокс"},
          {"Grenades", "Гранаты"},
          {"Dogs", "Собаки"},
          {"Vehicle", "Транспорт"},
      };

      const char *aimbot[][2] = {
    {"Enable", "Включить"},
    {"Visible Check", "Только видимых"},
    {"Ignore Bots", "Игнор Ботов"},
    {"Ignore Knocked", "Игнор Нокнутых"},
    {"Distance", "Дистанция"},
    {"FOV Size", "Размер Круга"},
    {"Smooth", "Плавность"},
    {"Recoil Control", "Контроль отдачи"}
};

      const char *memory[][2]{
          {"IPad View", "Айпад Вид"},
          {"Speedhack (Lying)", "Спидхак (Лёжа)"},
          {"165FPS Unlock", "Разблокировка 165 ФПС"}
      };

      ImGui::SetCursorPos(ImVec2(20.0f, 155.0f));
      if (ImGui::Button(menu[0][current_lang], ImVec2(197.0f, 39.0f))) {
        tabs = 0;
      }

      ImGui::SetCursorPos(ImVec2(20.0f, 195.0f));
      if (ImGui::Button(menu[1][current_lang],
                        ImVec2(197.0f, 39.0f))) {
        tabs = 1;
      }
      ImGui::SetCursorPos(ImVec2(20.0f, 235.0f));
      if (ImGui::Button(menu[2][current_lang],
                        ImVec2(197.0f, 39.0f))) {
        tabs = 2;
      }

      ImGui::SetCursorPos(ImVec2(20.0f, window_height - 49.0f - 13.0f));
      if (ImGui::Button(menu[3][current_lang], ImVec2(197.0f, 39.0f))) {
        tabs = 3;
      }

      float offset_x = 20.0f + 197.0f + 30.0f;
      ImVec2 p1 = ImVec2(pos.x + offset_x, pos.y);
      ImVec2 p2 = ImVec2(pos.x + offset_x, pos.y + window_height);
      ImGui::GetWindowDrawList()->AddLine(p1, p2, ImColor(61, 61, 61, 61),
                        3.0f);

      ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(172, 172, 172, 255));
      ImGui::SetCursorPos(ImVec2(offset_x + 30.0f, 20.0f));
      if (ImGui::Button(menu[4][current_lang], ImVec2(197.0f, 39.0f))) {
        current_lang = (current_lang == ENG) ? RU : ENG;
        OnConfigChanged();
      }
      ImGui::PopStyleColor();

      if (tabs == 0) {
        static int _wishIdx = -1;
        if (_wishIdx < 0) _wishIdx = rand() % 3;
        const char* _wishes[3][2] = {
          {"Good luck and have a great game!", "Удачной и упешной игры!"},
          {"May every game bring you victory!", "Пуст каждая игры принесёт победу!"},
          {"Enjoy the game and win!", "Наслаждайся игрой и побеждай!"}
        };

        ImGui::SetCursorPos(ImVec2(offset_x + 30.0f, 75.0f));
        ImGui::BeginChild("##home_tab", ImVec2(355.0f, window_height - 95.0f), true);

        ImGui::SetCursorPos(ImVec2(15.0f, 15.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(87, 87, 87, 255));
        ImGui::Text(menu[10][current_lang]);
        ImGui::PopStyleColor();

        ImGui::SetCursorPos(ImVec2(15.0f, 40.0f));
        ImGui::Text((current_lang == ENG) ? "Welcome to UNFAILMOD!" : "Добро пожаловать в UNFAILMOD!");

        ImGui::SetCursorPos(ImVec2(15.0f, 65.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(100, 200, 100, 255));
        ImGui::Text(_wishes[_wishIdx][current_lang]);
        ImGui::PopStyleColor();

        ImGui::SetCursorPos(ImVec2(15.0f, 100.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(172, 172, 172, 255));
        ImGui::Text((current_lang == ENG) ? "Key expires:" : "Ключ действительн до:");
        ImGui::PopStyleColor();

        ImGui::SetCursorPos(ImVec2(15.0f, 120.0f));
        ImGui::Text("%s", g_KeyExpiry.c_str());

        float _hbY = 155.0f;
        ImGui::SetCursorPos(ImVec2(15.0f, _hbY));
        if (ImGui::Button((current_lang == ENG) ? "Telegram" : "Телеграм", ImVec2(165.0f, 34.0f))) {
          OpenURL_Safe(ENCLKOVATE("https://t.me/UNFAILMOD"));
        }

        ImGui::SetCursorPos(ImVec2(190.0f, _hbY));
        if (ImGui::Button((current_lang == ENG) ? "Channel" : "Канал", ImVec2(165.0f, 34.0f))) {
          OpenURL_Safe(ENCLKOVATE("https://t.me/UNFAILMOD"));
        }

        _hbY += 44.0f;
        ImGui::SetCursorPos(ImVec2(15.0f, _hbY));
        if (ImGui::Button((current_lang == ENG) ? "Load Config" : "Загруить конфиг", ImVec2(165.0f, 34.0f))) {
          loadConfigBin();
        }

        ImGui::SetCursorPos(ImVec2(190.0f, _hbY));
        if (ImGui::Button((current_lang == ENG) ? "Save Config" : "Сохранить конфиг", ImVec2(165.0f, 34.0f))) {
          saveConfigBin();
        }

        ImGui::EndChild();
      }
      if (tabs == 1) {
        ImGui::SetCursorPos(ImVec2(offset_x + 30.0f, 75.0f));
        ImGui::BeginChild("##esp_tab", ImVec2(155.0f, window_height - 95.0f),
                        true);

        ImGui::SetWindowFontScale(0.9);

        ImU32 GrayText = IM_COL32(87, 87, 87, 255);

        ImGui::SetCursorPos(ImVec2(15.0f, 10.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, GrayText);
        ImGui::Text(menu[5][current_lang]);
        ImGui::PopStyleColor();

        ImGui::SetWindowFontScale(1.0f);

        float esp_padding = 30.0f;
        ImGui::SetCursorPos(ImVec2(15.0f, esp_padding));
        if (ImGui::Checkbox(esp_player[0][current_lang], &Config.ESP.Line))
          OnConfigChanged();

        esp_padding += 25.0f;
        ImGui::SetCursorPos(ImVec2(15.0f, esp_padding));
        if (ImGui::Checkbox(esp_player[1][current_lang], &Config.ESP.Name))
          OnConfigChanged();

        esp_padding += 25.0f;
        ImGui::SetCursorPos(ImVec2(15.0f, esp_padding));
        if (ImGui::Checkbox(esp_player[2][current_lang], &Config.ESP.Health))
          OnConfigChanged();

        esp_padding += 25.0f;
        ImGui::SetCursorPos(ImVec2(15.0f, esp_padding));
        if (ImGui::Checkbox(esp_player[3][current_lang], &Config.ESP.TeamID))
          OnConfigChanged();

        esp_padding += 25.0f;
        ImGui::SetCursorPos(ImVec2(15.0f, esp_padding));
        if (ImGui::Checkbox(esp_player[4][current_lang], &Config.ESP.Distance))
          OnConfigChanged();

        esp_padding += 25.0f;
        ImGui::SetCursorPos(ImVec2(15.0f, esp_padding));
        if (ImGui::Checkbox(esp_player[5][current_lang], &Config.ESP.Skeleton))
          OnConfigChanged();

        esp_padding += 25.0f;
        ImGui::SetCursorPos(ImVec2(15.0f, esp_padding));
        if (ImGui::Checkbox(esp_player[6][current_lang], &Config.ESP.Box))
          OnConfigChanged();

        esp_padding += 25.0f;
        ImGui::SetCursorPos(ImVec2(15.0f, esp_padding));
        if (ImGui::Checkbox(esp_player[7][current_lang], &Config.ESP.Alert))
          OnConfigChanged();

        esp_padding += 25.0f;
        ImGui::SetCursorPos(ImVec2(15.0f, esp_padding));
        if (ImGui::Checkbox(esp_player[8][current_lang], &Config.ESP.HideBot))
          OnConfigChanged();

        ImGui::EndChild();

        ImGui::SetCursorPos(ImVec2(offset_x + 195.0f, 75.0f));
        ImGui::BeginChild("##esp_world_tab",
                        ImVec2(200.0f, window_height - 220.0f), true);

        ImGui::SetWindowFontScale(0.9);

        ImGui::SetCursorPos(ImVec2(15.0f, 10.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, GrayText);
        ImGui::Text(menu[6][current_lang]);
        ImGui::PopStyleColor();

        ImGui::SetWindowFontScale(1.0f);

        float esp_world_padding = 30.0f;
        ImGui::SetCursorPos(ImVec2(15.0f, esp_world_padding));
        if (ImGui::Checkbox(esp_world[0][current_lang], &Config.ESP.Lootbox))
          OnConfigChanged();

        esp_world_padding += 25.0f;
        ImGui::SetCursorPos(ImVec2(15.0f, esp_world_padding));
        if (ImGui::Checkbox(esp_world[1][current_lang], &Config.ESP.Grenades))
          OnConfigChanged();

        esp_world_padding += 25.0f;
        ImGui::SetCursorPos(ImVec2(15.0f, esp_world_padding));
        if (ImGui::Checkbox(esp_world[2][current_lang], &Config.ESP.Dogs))
          OnConfigChanged();

        esp_world_padding += 25.0f;
        ImGui::SetCursorPos(ImVec2(15.0f, esp_world_padding));
        if (ImGui::Checkbox(esp_world[3][current_lang], &Config.ESP.Vehicle))
          OnConfigChanged();

        ImGui::EndChild();
      }
if (tabs == 2) {
    ImGui::SetCursorPos(ImVec2(offset_x + 30.0f, 75.0f));
    ImGui::BeginChild("##aimbot_tab", ImVec2(155.0f, window_height - 95.0f), true);

    ImGui::SetWindowFontScale(0.9f);
    ImU32 GrayText = IM_COL32(87, 87, 87, 255);

    ImGui::SetCursorPos(ImVec2(15.0f, 10.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, GrayText);
    ImGui::Text(menu[8][current_lang]);
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.0f);

    float aim_padding = 30.0f;

    ImGui::SetCursorPos(ImVec2(15.0f, aim_padding));
    if (ImGui::Checkbox(aimbot[0][current_lang], &Config.AimBot.Enable))
        OnConfigChanged();

    aim_padding += 25.0f;
    ImGui::SetCursorPos(ImVec2(15.0f, aim_padding));
        OnConfigChanged();

    aim_padding += 25.0f;
    ImGui::SetCursorPos(ImVec2(15.0f, aim_padding));
    if (ImGui::Checkbox(aimbot[2][current_lang], &Config.AimBot.IgnoreBots))
        OnConfigChanged();

    aim_padding += 25.0f;
    ImGui::SetCursorPos(ImVec2(15.0f, aim_padding));
    if (ImGui::Checkbox(aimbot[3][current_lang], &Config.AimBot.IgnoreKnocked))
        OnConfigChanged();

    aim_padding += 25.0f;
    ImGui::SetCursorPos(ImVec2(15.0f, aim_padding));
    if (ImGui::SliderFloat(aimbot[4][current_lang], &Config.AimBot.Distance, 0.0f, 390.0f))
        OnConfigChanged();

    aim_padding += 40.0f;
    ImGui::SetCursorPos(ImVec2(15.0f, aim_padding));
    if (ImGui::SliderFloat(aimbot[5][current_lang], &Config.AimBot.Cross, 0.0f, 400.0f))
        OnConfigChanged();

    aim_padding += 40.0f;
    ImGui::SetCursorPos(ImVec2(15.0f, aim_padding));
    if (ImGui::SliderFloat(aimbot[6][current_lang], &Config.AimBot.Smooth, 1.0f, 20.0f))
        OnConfigChanged();

    aim_padding += 40.0f;
    ImGui::SetCursorPos(ImVec2(15.0f, aim_padding));
    if (ImGui::SliderFloat(aimbot[7][current_lang], &Config.AimBot.RecoilControl, 0.0f, 10.0f))
        OnConfigChanged();

    ImGui::EndChild();

    ImGui::SetCursorPos(ImVec2(offset_x + 195.0f, 75.0f));
    ImGui::BeginChild("##aimbot_settings", ImVec2(200.0f, window_height - 95.0f), true);

    ImGui::SetWindowFontScale(0.9f);
    ImGui::SetCursorPos(ImVec2(15.0f, 10.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, GrayText);
    ImGui::Text(menu[9][current_lang]);
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.0f);

    float aimbot_settings_padding = 30.0f;

    ImGui::SetCursorPos(ImVec2(15.0f, aimbot_settings_padding));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(172, 172, 172, 255));
    ImGui::Text((current_lang == ENG) ? "Trigger" : "Триггер");
    ImGui::PopStyleColor();
    aimbot_settings_padding += 21.0f;
    ImGui::SetCursorPos(ImVec2(15.0f, aimbot_settings_padding));
    {
        int triggerIdx = (int)Config.AimBot.Trigger;
        if (KYBR::CustomCombo("##trigger", &triggerIdx,
              [](int i, void*) { return aimTriggerLabels[i][current_lang]; }, 5)) {
            Config.AimBot.Trigger = (EAimTrigger)triggerIdx;
            OnConfigChanged();
        }
    }

    aimbot_settings_padding += 50.0f;
    ImGui::SetCursorPos(ImVec2(15.0f, aimbot_settings_padding));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(172, 172, 172, 255));
    ImGui::Text((current_lang == ENG) ? "Mode" : "Режим");
    ImGui::PopStyleColor();
    aimbot_settings_padding += 21.0f;
    ImGui::SetCursorPos(ImVec2(15.0f, aimbot_settings_padding));
    {
        int modeIdx = (int)Config.AimBot.Target;
        if (KYBR::CustomCombo("##mode", &modeIdx,
              [](int i, void*) { return aimModeLabels[i][current_lang]; }, 4)) {
            Config.AimBot.Target = (EAimTarget)modeIdx;
            OnConfigChanged();
        }
    }

    ImGui::EndChild();
}
      if (tabs == 3) {
        ImGui::SetCursorPos(ImVec2(offset_x + 50.0f, 75.0f));
        ImGui::BeginChild("##memory_tab",
                        ImVec2(200.0f, window_height - 255.0f), true,
                        ImGuiWindowFlags_NoScrollbar);

        ImGui::SetWindowFontScale(0.9);

        ImU32 GrayText = IM_COL32(87, 87, 87, 255);

        ImGui::SetCursorPos(ImVec2(15.0f, 10.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, GrayText);
        ImGui::Text(menu[7][current_lang]);
        ImGui::PopStyleColor();

        ImGui::SetWindowFontScale(1.0f);

        float memory_padding = 30.0f;
        ImGui::SetCursorPos(ImVec2(15.0f, memory_padding));
        if (ImGui::Checkbox(memory[0][current_lang], &Config.Memory.IPad))
          OnConfigChanged();

        memory_padding += 25.0f;
        ImGui::SetCursorPos(ImVec2(15.0f, memory_padding));
        if (ImGui::Checkbox(memory[1][current_lang], &Config.Memory.Speedhack))
          OnConfigChanged();

        memory_padding += 25.0f;
        ImGui::SetCursorPos(ImVec2(15.0f, memory_padding));
        if (ImGui::Checkbox(memory[2][current_lang], &Config.Memory.Unlock))
          OnConfigChanged();

        ImGui::EndChild();
      }
    }
    ImGui::End();
  }
  }

  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  return orig_eglSwapBuffers(dpy, surface);
}

SUCHKA(int, AInputQueue_getEvent,
      (AInputQueue * queue, AInputEvent **outEvent)) {
  int result = oAInputQueue_getEvent(queue, outEvent);
  if (result >= 0 && *outEvent != nullptr && initImGui) {
    ImGui_ImplAndroid_HandleInputEvent(*outEvent,
                        {(float)screenWidth / (float)glWidth,
                        (float)screenHeight / (float)glHeight});
  }
  return result;
}

static void (*oProcessEvent)(UObject* Object, UFunction* Function, void* Parms, void* Result) = nullptr;
static void hkProcessEvent(UObject* Object, UFunction* Function, void* Parms, void* Result) {
    if (oProcessEvent) {
        oProcessEvent(Object, Function, Parms, Result);
    }
}


[[noreturn]] void *maps_thread(void *) {
  while (true) {
    auto t1 = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();

    std::vector<sRegion> tmp;
    char line[512];
    FILE *f = fopen("/proc/self/maps", "r");
    if (f) {
      // ---- Применение патчей памяти ----
      if (g_LocalPlayer && !isObjectInvalid(g_LocalPlayer)) {
        if (Config.Memory.Speedhack && g_LocalPlayer->CharacterMovement) {
          g_LocalPlayer->CharacterMovement->GravityScale = 0.0f;
          g_LocalPlayer->EnergySpeedScale = 1.5f;
          g_LocalPlayer->CharacterMovement->MaxWalkSpeedCrouched = 800.f;
        } else if (g_LocalPlayer->CharacterMovement) {
          if (g_LocalPlayer->CharacterMovement->GravityScale == 0.0f)
            g_LocalPlayer->CharacterMovement->GravityScale = 1.0f;
          if (g_LocalPlayer->EnergySpeedScale == 1.5f)
            g_LocalPlayer->EnergySpeedScale = 1.0f;
          if (g_LocalPlayer->CharacterMovement->MaxWalkSpeedCrouched == 800.f)
            g_LocalPlayer->CharacterMovement->MaxWalkSpeedCrouched = 135.f;
        }
      }

      if (Config.Memory.Unlock) {
        auto objs = UObject::GetGlobalObjects();
        for (int i = 0; i < objs.Num(); i++) {
          auto Object = objs.GetByIndex(i);
          if (isObjectInvalid(Object)) continue;
          if (Object->IsA(USTExtraGameInstance::StaticClass())) {
            auto gi = (USTExtraGameInstance *)Object;
            if (gi->UserDetailSetting.PUBGDeviceFPSHigh != 165) {
              gi->UserDetailSetting.PUBGDeviceFPSDef        = 165;
              gi->UserDetailSetting.PUBGDeviceFPSLow        = 165;
              gi->UserDetailSetting.PUBGDeviceFPSMid        = 165;
              gi->UserDetailSetting.PUBGDeviceFPSHigh       = 165;
              gi->UserDetailSetting.PUBGDeviceFPSHDR        = 165;
              gi->UserDetailSetting.PUBGDeviceFPSUltralHigh = 165;
            }
          }
        }
      }

      if (Config.Memory.IPad) {
        auto objs = UObject::GetGlobalObjects();
        for (int i = 0; i < objs.Num(); i++) {
          auto Object = objs.GetByIndex(i);
          if (isObjectInvalid(Object)) continue;
          if (Object->IsA(ULocalPlayer::StaticClass())) {
            auto lp = (ULocalPlayer *)Object;
            if (lp->AspectRatioAxisConstraint != EAspectRatioAxisConstraint::AspectRatio_MaintainYFOV)
              lp->AspectRatioAxisConstraint = EAspectRatioAxisConstraint::AspectRatio_MaintainYFOV;
          }
        }
      } else {
        auto objs = UObject::GetGlobalObjects();
        for (int i = 0; i < objs.Num(); i++) {
          auto Object = objs.GetByIndex(i);
          if (isObjectInvalid(Object)) continue;
          if (Object->IsA(ULocalPlayer::StaticClass())) {
            auto lp = (ULocalPlayer *)Object;
            if (lp->AspectRatioAxisConstraint == EAspectRatioAxisConstraint::AspectRatio_MaintainYFOV)
              lp->AspectRatioAxisConstraint = EAspectRatioAxisConstraint::AspectRatio_MaintainXFOV;
          }
        }
      }

      // ---------------- ИЗМЕНЕНИЕ 4: ОЧИСТКА ОТ ЛОЖНЫХ ДЕТЕКТОВ СВОИХ ЛИБ ----------------
      // Убраны libdobby.so, libSSHook.so, libsubstrate.so, libhydra.so и др. 
      // которые загружает сам мод, чтобы не триггерить `g_CrackDetected` на старте.
      char b0[16] = {'l','i','b','M','r','3','u','b','a','.','s','o',0};
      char b1[16] = {'l','i','b','i','n','j','e','c','t','.','s','o',0};
      char b2[16] = {'l','i','b','f','r','i','d','a','.','s','o',0};
      char b6[16] = {'l','i','b','r','e','r','o','o','t','.','s','o',0};
      char b7[16] = {'l','i','b','x','p','o','s','e','d','.','s','o',0};
      const char* banned[] = {b0,b1,b2,b6,b7,nullptr};
      const char* banned_ext[] = {nullptr};

      // ---- Чтение maps и проверка ----
      while (fgets(line, sizeof(line), f)) {
        uintptr_t start, end;
        char tmpProt[16];
        if (sscanf(line, "%" PRIXPTR "-%" PRIXPTR " %16s %*s %*s %*s %*s",
                  &start, &end, tmpProt) > 0) {
          if (tmpProt[0] != 'r') {
            tmp.push_back({start, end});
          }
        }
        // Проверка banned
        for (int bi = 0; banned[bi] != nullptr; bi++) {
          if (strstr(line, banned[bi])) {
            g_CrackDetected = true;
            _SetValid(false);
            g_MenuOpen = false;
            bValid = false;
          }
        }
        // Проверка banned_ext (все удалены, чтобы не детектить мод)
      }
      fclose(f);
    }

    // ---- Проверка TracerPid ----
    {
      FILE *_tf = fopen("/proc/self/status", "r");
      if (_tf) {
        char _tl[256];
        while (fgets(_tl, sizeof(_tl), _tf)) {
          if (strstr(_tl, "TracerPid:") && !strstr(_tl, "TracerPid:	0")) {
            g_CrackDetected = true;
            _SetValid(false);
            g_MenuOpen = false;
            bValid = false;
          }
        }
        fclose(_tf);
      }
    }

    // ---- Проверка потоков (Frida и др.) ----
    {
      DIR *_td = opendir("/proc/self/task");
      if (_td) {
        struct dirent *_te;
        while ((_te = readdir(_td)) != nullptr) {
          char _tp[128];
          snprintf(_tp, sizeof(_tp), "/proc/self/task/%s/comm", _te->d_name);
          FILE *_tc = fopen(_tp, "r");
          if (_tc) {
            char _tn[64];
            if (fgets(_tn, sizeof(_tn), _tc)) {
              if (strstr(_tn, "gmain") || strstr(_tn, "gdbus") || strstr(_tn, "pool-frida")) {
                g_CrackDetected = true;
                _SetValid(false);
                g_MenuOpen = false;
                bValid = false;
              }
            }
            fclose(_tc);
          }
        }
        closedir(_td);
      }
    }

    trapRegions = tmp;
    auto td = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count() -
              t1;
    std::this_thread::sleep_for(std::chrono::milliseconds(
        std::max(std::min(0LL, 100 - td), 100LL)));
  }
}


#define libanogs "libanogs.so"
#define _BYTE uint8_t
#define _WORD uint16_t
#define _DWORD uint32_t
#define _QWORD uint64_t
#define PATCH_LIB
#define HOOK_LIB(lib, addr, hook, orig)
#define HOOK_LIB_NO_ORIG(lib, offset, func)
#define _BYTE unsigned char
#define _WORD unsigned short
#define _DWORD unsigned int
#define _QWORD unsigned long long


#define xor_str(s)                                                             \
  ([]() {                                                                      \
    static char _[] = s;                                                       \
    for (int i = 0; _[i] != '\0'; ++i)                                         \
      _[i] ^= 0x5A;                                                            \
    return _;                                                                  \
  })()

#define NOOB(RET, NAME, ARGS)                                                  \
  RET(*o##NAME) ARGS;                                                          \
  RET h##NAME ARGS

#define SONIC(RET, NAME, ARGS) \
  RET(*o##NAME) ARGS; \
  RET h##NAME ARGS


#define sub_1CA794
#define ARM64_SYSREG
#define HOOK_LIB


#ifdef DWORD
#undef DWORD
#endif
#ifdef _DWORD
#undef _DWORD
#endif


#define HIWORD
#define __strncpy_chk
#define __memcpy_chk
#define __strncpy_chk2
#define __fgets_chk
#define __errno
#define qmemcpy

#define byte_4
#define _ReadStatusReg
#define BYTE5
#define BYTE4
#define HIBYTE
#define BYTE6
#define IsMemoryReadable
#define BYTE1
#define BYTE3
char *dword_57F060;
char *qword_1C9D48;
char *qword_57CE08;
char *stru_57CE10;
char *dword_579D28;
uint32_t dword_5755A4;

#define BYTE2
#define _WriteStatusReg
#define LOBYTE
#define BYTE


static bool _IsHooked(void *fn) {
  if (!fn) return false;
  const uint8_t *p = (const uint8_t *)fn;
  uint32_t instr;
  memcpy(&instr, p, 4);
  if ((instr & 0xFF000000u) == 0x58000000u) return true;
  if ((instr & 0xFC000000u) == 0x14000000u) return true;
  if ((instr & 0xFFFFFC1Fu) == 0xD63F0000u) return true;
  return false;
}

static void _CheckHookIntegrity() {
  if (_IsHooked((void*)_CheckValid) ||
      _IsHooked((void*)_SetValid)   ||
      _IsHooked((void*)orig_eglSwapBuffers)) {
    if (bValid) {
      g_CrackDetected = true;
      _SetValid(false);
      g_MenuOpen = false;
    }
  }
}

// ---------------- ИЗМЕНЕНИЕ 5: Исправление потока проверки целостности ----------------
[[noreturn]] static void *_IntegrityThread(void *) {
  // Ждём, пока не инициализируются данные о секции .rodata, чтобы избежать краша
  while (g_RodataStart == 0) {
      sleep(1);
  }
  while (true) {
    sleep(2);
    if (_IsFunctionHooked((void*)_CheckValid) || _IsFunctionHooked((void*)_SetValid)) {
      if (bValid) {
        g_CrackDetected = true;
        bValid = false;
        g_MenuOpen = false;
      }
    }
    _CheckHookIntegrity();
    {
      // Используем динамически найденные адреса, старые переменные удалены
      uint32_t _liveCrc = _CRC32Snippet((void*)g_RodataStart, g_RodataSize);
      if (_liveCrc != _rodata_crc) {
        g_CrackDetected = true;
        bValid = false;
        g_MenuOpen = false;
      }
    }
    _CV_IsValid();
    if (g_MenuOpen && !_CV_IsValid()) {
      g_MenuOpen = false;
      g_CrackDetected = true;
    }
  }
}


static void FixGameCrash()
{
    system("rm -rf /data/data/com.pubg.imobile/files");
    system("rm -rf /data/data/com.pubg.imobile/files/ano_tmp");
    system("touch /data/data/com.pubg.imobile/files/ano_tmp");
    system("chmod 000 /data/data/com.pubg.imobile/files/ano_tmp");
    system("rm -rf /data/data/com.pubg.imobile/files/obblib");
    system("touch /data/data/com.pubg.imobile/files/obblib");
    system("chmod 000 /data/data/com.pubg.imobile/files/obblib");
    system("rm -rf /data/data/com.pubg.imobile/files/xlog");
    system("touch /data/data/com.pubg.imobile/files/xlog");
    system("chmod 000 /data/data/com.pubg.imobile/files/xlog");
    system("rm -rf /data/data/com.pubg.imobile/app_bugly");
    system("touch /data/data/com.pubg.imobile/app_bugly");
    system("chmod 000 /data/data/com.pubg.imobile/app_bugly");
    system("rm -rf /data/data/com.pubg.imobile/app_crashrecord");
    system("touch /data/data/com.pubg.imobile/app_crashrecord");
    system("chmod 000 /data/data/com.pubg.imobile/app_crashrecord");
    system("rm -rf /data/data/com.pubg.imobile/app_crashSight");
    system("touch /data/data/com.pubg.imobile/app_crashSight");
    system("chmod 000 /data/data/com.pubg.imobile/app_crashSight");
}

void *main_thread(void *) {
  UE4 = 0; { int _ue4Timeout = 0; while (!UE4 && _ue4Timeout < 30) { UE4 = Tools::GetBaseAddress(ENCLKOVATE("libUE4.so")); if (!UE4) { sleep(1); _ue4Timeout++; } } if (!UE4) return nullptr; }
  int timeout = 0; while (!g_App && timeout < 30) { g_App = *(android_app **)(UE4 + GNativeAndroidApp_Offset); if (!g_App) { sleep(1); timeout++; } }
  pthread_t _integrityTid; pthread_create(&_integrityTid, nullptr, _IntegrityThread, nullptr); pthread_detach(_integrityTid);
  if (g_App && g_App->activity) { JNIEnv* env = nullptr; g_App->activity->vm->AttachCurrentThread(&env, nullptr); if (env) { Java_com_your_app_MainActivity_initBypass(env, g_App->activity->clazz); g_App->activity->vm->DetachCurrentThread(); } }
  void *libAndroid = dlopen(ENCLKOVATE("libandroid.so"), RTLD_NOW); if (libAndroid) { void *symEvent = dlsym(libAndroid, ENCLKOVATE("AInputQueue_getEvent")); if (symEvent) { DobbyHook(symEvent, (void *)hAInputQueue_getEvent, (void **)&oAInputQueue_getEvent); } dlclose(libAndroid); }
  FName::GNames = GetGNames(); int _gnTimeout = 0; while (!FName::GNames && _gnTimeout < 15) { sleep(1); _gnTimeout++; FName::GNames = GetGNames(); } if (!FName::GNames) FName::GNames = (TNameEntryArray*)(UE4 + GNames_Offset);
  UObject::GUObjectArray = (FUObjectArray *)(UE4 + GUObject_Offset);
  void *egl = dlopen("libEGL.so", RTLD_NOW | RTLD_GLOBAL); if (!egl) egl = dlopen("/system/lib64/libEGL.so", RTLD_NOW | RTLD_GLOBAL); int _eglTimeout = 0; while (!egl && _eglTimeout < 10) { sleep(1); _eglTimeout++; egl = dlopen("libEGL.so", RTLD_NOW | RTLD_GLOBAL); } void *eglSym = egl ? dlsym(egl, "eglSwapBuffers") : nullptr; if (eglSym) { DobbyHook(eglSym, (void *)_eglSwapBuffers, (void **)&orig_eglSwapBuffers); }
  DobbyHook((void *)(UE4 + ProcessEvent_Offset), (void *)hkProcessEvent, (void **)&oProcessEvent);
  pthread_t t; pthread_create(&t, 0, maps_thread, 0);
  { std::string savedKey = LoadKey(); if (!savedKey.empty()) { { std::lock_guard<std::mutex> lk(g_LoginMutex); g_LoginState = LoginState::LOADING; } if (g_LoginThread.joinable()) g_LoginThread.join(); g_LoginThread = std::thread([savedKey]() { std::string result = Login(savedKey.c_str()); std::lock_guard<std::mutex> lk(g_LoginMutex); _CheckValid(); if (_CV_IsValid()) { g_LoginState = LoginState::SUCCESS; } else { DeleteKeyFile(); g_LoginError = result; g_LoginState = LoginState::ERROR_MSG; } }); } }
  return nullptr;
}

__attribute__((constructor(102))) void _init() { pthread_t t; pthread_create(&t, 0, main_thread, 0); }