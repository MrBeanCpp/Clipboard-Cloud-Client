#ifndef WEBICONFETCHER_H
#define WEBICONFETCHER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QSaveFile>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QUrl>
#include <QDebug>

#include <functional>

class WebIconFetcher : public QObject {
    // Q_OBJECT
public:
    using Callback = std::function<void(QString localPath)>;

    explicit WebIconFetcher(QNetworkAccessManager* netManager,
                            QObject* parent = nullptr,
                            int timeoutMs = 2000,
                            QString cacheSubDir = "DogPaw_favicons")
        : QObject(parent),
          netManager(netManager),
          timeoutMs(timeoutMs),
          cacheDirPath(QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                       + "/" + std::move(cacheSubDir))
    {
        // 确保缓存目录存在
        QDir().mkpath(cacheDirPath);
    }

    void setTimeoutMs(int ms) { timeoutMs = ms; }

    // 对外 API：给定网页 URL，下载并缓存网站图标，回调本地文件路径；失败回调空字符串
    void fetch(const QString& pageUrlStr, Callback cb)
    {
        if (!netManager) { cb({}); return; }

        const QUrl pageUrl = QUrl::fromUserInput(pageUrlStr.trimmed());
        if (!isHttpUrl(pageUrl)) { cb({}); return; }

        const QString host = pageUrl.host().toLower();
        const QString baseName = cacheBaseName(host);

        // 0) 缓存命中：同 host 只要已有任何后缀文件（baseName.*）就直接返回，减少网络请求
        if (const QString hit = findCachedFile(baseName); !hit.isEmpty()) {
            qDebug().noquote() << "[webicon] cache hit" << host << "->" << QDir::toNativeSeparators(hit);
            cb(QDir::toNativeSeparators(hit));
            return;
        }

        // 1) 先试 /favicon.ico（最快捷的传统路径）
        QUrl icoUrl = pageUrl;
        icoUrl.setPath("/favicon.ico");
        icoUrl.setQuery({});
        icoUrl.setFragment({});
        icoUrl.setUserInfo({});

        get(icoUrl, /*acceptHtml=*/false, /*acceptImage=*/true, [=](Resp r) mutable {
            log("[webicon] /favicon.ico", r);

            // 2xx 且有 body：直接按“原后缀/推断后缀”写入缓存
            if (r.is2xx() && !looksLikeNotAnIcon(r.contentType, r.body)) {
                writeHostIconFile(baseName, r.finalUrl, r.contentType, r.body, cb);
                return;
            }

            // 1.1) 注意：即使 acceptImage=true，服务器也可能返回 HTML 错误页（404 页面）
            // 这种 HTML 里可能包含 <link rel=icon href=...>，我们可以顺便解析一次作为捷径
            if (!r.body.isEmpty() && isHtml(r.contentType)) {
                resolveIconFromHtml(baseName, r, pageUrl, cb, /*allowNon2xxHtml=*/true);
                return;
            }

            // 2) fallback：拉取页面 HTML -> 解析 icon -> 下载 icon
            get(pageUrl, /*acceptHtml=*/true, /*acceptImage=*/false, [=](Resp p) mutable {
                log("[webicon] page html", p);
                resolveIconFromHtml(baseName, p, pageUrl, cb, /*allowNon2xxHtml=*/false);
            });
        });
    }

    static bool isHttpUrl(const QUrl& u) {
        if (!u.isValid() || u.host().isEmpty()) return false;
        const QString s = u.scheme().toLower();
        return s == "http" || s == "https";
    }

    static bool isHtml(const QString& ctype) {
        return ctype.contains("text/html", Qt::CaseInsensitive);
    }

private:
    QNetworkAccessManager* netManager = nullptr;
    int timeoutMs = 2000;
    QString cacheDirPath;

    // 默认 User-Agent（模仿主流浏览器，避免被部分站点拒绝访问）
    static constexpr const char* kUserAgent =
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        "AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/120.0 Safari/537.36";

    // 网络响应信息（把 QNetworkReply 的常用字段拎出来，避免 deleteLater 后访问 reply）
    struct Resp {
        int status = 0;
        QString contentType;
        QNetworkReply::NetworkError err = QNetworkReply::NoError;
        QString errStr;
        QUrl finalUrl;
        QByteArray body;
        qint64 costMs = 0;

        bool is2xx() const { return err == QNetworkReply::NoError && status >= 200 && status < 300; }
        bool isHtml2xx() const {
            return is2xx() && !body.isEmpty()
                && contentType.contains("text/html", Qt::CaseInsensitive);
        }
    };

    // 为请求设置通用属性：超时、重定向策略、UA、Accept
    static void applyCommon(QNetworkRequest& req, int timeoutMs, bool acceptHtml, bool acceptImage) {
        req.setTransferTimeout(timeoutMs);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);

        if (acceptHtml) {
            req.setRawHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
        } else if (acceptImage) {
            req.setRawHeader("Accept", "image/avif,image/webp,image/apng,image/*,*/*;q=0.8");
        }
    }

    // get(): 发起 GET 请求并回调 Resp（统一封装 finished 处理）
    void get(const QUrl& url, bool acceptHtml, bool acceptImage, std::function<void(Resp)> done) const
    {
        QNetworkRequest req(url);
        applyCommon(req, timeoutMs, acceptHtml, acceptImage);

        QElapsedTimer t; t.start();
        QNetworkReply* reply = netManager->get(req);

        QObject::connect(reply, &QNetworkReply::finished, reply, [reply, done, t]() mutable {
            qDebug() << "[SSL protocol]" << reply->sslConfiguration().protocol();

            Resp r;
            r.costMs = t.elapsed();
            r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            r.contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
            r.err = reply->error();
            r.errStr = reply->errorString();
            r.finalUrl = reply->url();
            r.body = reply->readAll();
            reply->deleteLater();
            done(std::move(r));
        });

        QObject::connect(reply, &QNetworkReply::sslErrors, reply, [url = req.url()](const QList<QSslError>& errors) {
            qWarning().noquote() << "[sslErrors]" << url.toString();
            for (const auto& e : errors) {
                qWarning().noquote() << " -" << e.errorString();
            }
        });
    }

    // douyin的/favicon.ico会返回 {}
    static bool looksLikeNotAnIcon(const QString& ctype, const QByteArray& body)
    {
        const QByteArray t = body.trimmed();
        if (t.isEmpty()) return true;

        // 仅用大小做兜底过滤：{} / [] / null / "ok" 这种
        if (t.size() <= 8) return true;

        // 轻量内容特征：JSON / HTML
        const char c0 = t[0];
        if (c0 == '{' || c0 == '[') return true;
        if (c0 == '<') return true;

        // 可选：content-type 明确指向文本/JSON，直接排除
        const QString ct = ctype.toLower();
        if (ct.contains("application/json") || ct.contains("text/html") || ct.contains("text/plain"))
            return true;

        return false;
    }

    static void log(const char* tag, const Resp& r) {
        qDebug().noquote()
            << tag
            << "cost=" << r.costMs << "ms"
            << "status=" << r.status
            << "ctype=" << r.contentType
            << "err=" << int(r.err) << r.errStr
            << "url=" << r.finalUrl.toString()
            << "bytes=" << r.body.size();
    }

    // host -> baseName（同 host 共用同一个 cache key）
    QString cacheBaseName(const QString& host) const
    {
        const QByteArray key = QCryptographicHash::hash(host.toUtf8(), QCryptographicHash::Sha1).toHex();
        return QString("webicon_%1").arg(QString::fromLatin1(key));
    }

    // 在缓存目录中用通配符查找 baseName.*，返回“最新修改”的那一个
    QString findCachedFile(const QString& baseName) const
    {
        QDir dir(cacheDirPath);
        QString best;
        qint64 bestMtime = -1;

        QDirIterator it(dir.absolutePath(), { baseName + ".*" }, QDir::Files);
        while (it.hasNext()) {
            const QString p = it.next();
            QFileInfo fi(p);
            if (fi.size() <= 0) continue;
            const qint64 mt = fi.lastModified().toMSecsSinceEpoch();
            if (mt > bestMtime) { bestMtime = mt; best = p; }
        }
        return best;
    }

    // 清理某个 host 的旧缓存文件（baseName.*），保证同 host 只保留一个图标文件
    void removeOldCachedFiles(const QString& baseName) const
    {
        QDir dir(cacheDirPath);
        QDirIterator it(dir.absolutePath(), { baseName + ".*" }, QDir::Files);
        while (it.hasNext()) QFile::remove(it.next());
    }

    // 原子写文件（避免写到一半崩溃导致缓存损坏）
    static bool saveAtomic(const QString& path, const QByteArray& bytes, QString* err = nullptr) {
        QSaveFile f(path);
        if (!f.open(QIODevice::WriteOnly)) { if (err) *err = f.errorString(); return false; }
        if (f.write(bytes) != bytes.size()) { f.cancelWriting(); if (err) *err = f.errorString(); return false; }
        if (!f.commit()) { if (err) *err = f.errorString(); return false; }
        return true;
    }

    // 推断文件后缀：优先使用 URL 后缀，其次根据 Content-Type 做粗略推断，最后兜底 bin
    static QString inferExt(const QUrl& url, const QString& contentType)
    {
        QString ext = QFileInfo(url.path()).suffix().toLower();
        if (!ext.isEmpty()) return ext;

        const QString ct = contentType.toLower();
        if (ct.contains("png")) return "png";
        if (ct.contains("jpeg")) return "jpg";
        if (ct.contains("jpg")) return "jpg";
        if (ct.contains("webp")) return "webp";
        if (ct.contains("gif")) return "gif";
        if (ct.contains("svg")) return "svg";
        if (ct.contains("icon") || ct.contains("x-icon")) return "ico";
        return "bin";
    }

    // writeHostIconFile(): 将图标 bytes 写入缓存目录，文件名为 baseName.<ext>，并回调本地路径
    void writeHostIconFile(const QString& baseName,
                           const QUrl& finalUrl,
                           const QString& contentType,
                           const QByteArray& bytes,
                           Callback cb) const
    {
        const QString ext = inferExt(finalUrl, contentType);
        const QString outPath = QDir(cacheDirPath).filePath(baseName + "." + ext);
        const QString nativeOut = QDir::toNativeSeparators(outPath);

        // 同 host 只保留一个图标文件：先清理旧的 baseName.* 再写新的
        removeOldCachedFiles(baseName);

        QString err;
        if (!saveAtomic(outPath, bytes, &err)) {
            qWarning().noquote() << "[webicon] save failed" << nativeOut << err;
            cb({});
            return;
        }

        qDebug().noquote() << "[webicon] saved" << nativeOut << "bytes=" << bytes.size();
        cb(nativeOut);
    }

    // 下载解析到的 iconUrl，然后写入 host 缓存并回调路径
    void downloadIconAndSave(const QString& baseName, const QUrl& iconUrl, Callback cb) const
    {
        // 并发/重复调用时再查一次缓存，避免重复下载
        if (const QString hit = findCachedFile(baseName); !hit.isEmpty()) {
            cb(QDir::toNativeSeparators(hit));
            return;
        }

        get(iconUrl, /*acceptHtml=*/false, /*acceptImage=*/true, [=](Resp r) mutable {
            log("[webicon] icon download", r);
            if (!r.is2xx() || r.body.isEmpty()) { cb({}); return; }
            writeHostIconFile(baseName, r.finalUrl, r.contentType, r.body, cb);
        });
    }

    // resolveIconFromHtml(): 对 HTML 响应解析 iconUrl，并下载缓存；失败则回调空字符串
    // allowNon2xxHtml=true 用于 /favicon.ico 返回的 404 HTML 错误页这种“捷径解析”场景
    void resolveIconFromHtml(const QString& baseName,
                             const Resp& htmlResp,
                             const QUrl& fallbackBaseUrl,
                             Callback cb,
                             bool allowNon2xxHtml) const
    {
        const bool htmlOk = allowNon2xxHtml
            ? (isHtml(htmlResp.contentType) && !htmlResp.body.isEmpty())
            : htmlResp.isHtml2xx();

        if (!htmlOk) { cb({}); return; }

        const QUrl base = htmlResp.finalUrl.isValid() ? htmlResp.finalUrl : fallbackBaseUrl;
        const QUrl iconUrl = parseIconUrlFromHtml(htmlResp.body, base);
        // 有些网站是动态的（Vue React），就没招了 得用WebEngine
        qDebug() << "[webicon] parsed iconUrl from html:" << iconUrl.toString();
        if (!isHttpUrl(iconUrl)) { cb({}); return; }

        downloadIconAndSave(baseName, iconUrl, cb);
    }

    // 从 HTML 中解析 icon URL：扫描 <link ...>，选择优先级最高的（修复 rel 优先级 + href 支持无引号）
    static QUrl parseIconUrlFromHtml(const QByteArray& html, const QUrl& baseUrl)
    {
        static const QRegularExpression linkRe(R"(<link\b[^>]*>)",
            QRegularExpression::CaseInsensitiveOption);

        static const QRegularExpression relRe(R"(\brel\s*=\s*["']([^"']+)["'])",
            QRegularExpression::CaseInsensitiveOption);

        // href 支持 "..." 或 '...' 或无引号 token
        static const QRegularExpression hrefRe(
            R"RAW(\bhref\s*=\s*(?:"([^"]+)"|'([^']+)'|([^\s>]+)))RAW",
            QRegularExpression::CaseInsensitiveOption);

        QUrl best;
        int bestScore = 1e9;

        const QString s = QString::fromUtf8(html);
        auto it = linkRe.globalMatch(s);
        while (it.hasNext()) {
            const QString tag = it.next().captured(0);

            const auto rm = relRe.match(tag);
            const auto hm = hrefRe.match(tag);
            if (!rm.hasMatch() || !hm.hasMatch()) continue;

            const QString rel = rm.captured(1).toLower();

            QString href = hm.captured(1);
            if (href.isEmpty()) href = hm.captured(2);
            if (href.isEmpty()) href = hm.captured(3);
            href = href.trimmed();
            if (href.isEmpty()) continue;

            // rel 优先级：shortcut icon > icon > apple-touch-icon
            // 注意：apple-touch-icon 必须在 icon 判断之前，否则会被 “contains(icon)” 吞掉
            int score = 1e9;
            if (rel.contains("shortcut icon")) score = 0;
            else if (rel.contains("apple-touch-icon")) score = 2;
            else if (rel.contains("icon")) score = 1;
            else continue;

            const QUrl u = baseUrl.resolved(QUrl(href));
            if (!isHttpUrl(u)) continue;

            if (score < bestScore) { bestScore = score; best = u; }
        }

        return best;
    }
};

#endif // WEBICONFETCHER_H
