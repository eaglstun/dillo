import Foundation

/// Fetches a page and its stylesheets with URLSession, decodes charsets,
/// and hands the bridge UTF-8 text. This replaces the desktop's
/// cache/capi/IO stack: all subresources are prefetched BEFORE parsing so
/// the parser's synchronous cached-CSS path can serve them.
final class PageLoader {
    struct Page {
        let html: String
        let finalURL: String
        /// absolute URL → UTF-8 CSS text
        let stylesheets: [String: String]
    }

    enum LoadError: Error, LocalizedError {
        case badURL
        case notHTML(String)
        case transport(Error)

        var errorDescription: String? {
            switch self {
            case .badURL: return "Not a valid URL."
            case .notHTML(let t): return "Unsupported content type: \(t)"
            case .transport(let e): return e.localizedDescription
            }
        }
    }

    private let session: URLSession
    /// Resolves an href against a base URL using dillo's url.c, so the
    /// cache keys match what the parser will compute for the same href.
    private let resolve: (_ href: String, _ base: String) -> String?

    init(resolver: @escaping (_ href: String, _ base: String) -> String?) {
        let config = URLSessionConfiguration.default
        config.httpAdditionalHeaders = [
            "User-Agent": "Dillo/3.3 (iOS spike)",
            "Accept": "text/html,application/xhtml+xml,text/*;q=0.8",
        ]
        config.timeoutIntervalForRequest = 20
        self.session = URLSession(configuration: config)
        self.resolve = resolver
    }

    func load(_ urlString: String,
              method: String = "GET",
              body: Data? = nil,
              bodyContentType: String? = nil,
              completion: @escaping (Result<Page, LoadError>) -> Void) {
        guard let url = URL(string: urlString), url.scheme != nil else {
            completion(.failure(.badURL))
            return
        }

        var request = URLRequest(url: url)
        request.httpMethod = method
        if let body = body {
            request.httpBody = body
            if let ct = bodyContentType {
                request.setValue(ct, forHTTPHeaderField: "Content-Type")
            }
        }

        session.dataTask(with: request) { data, response, error in
            if let error = error {
                DispatchQueue.main.async {
                    completion(.failure(.transport(error)))
                }
                return
            }
            let data = data ?? Data()
            let http = response as? HTTPURLResponse
            let contentType =
                (http?.value(forHTTPHeaderField: "Content-Type") ?? "text/html")
                .lowercased()

            let finalURL = (response?.url ?? url).absoluteString
            let isHTML = contentType.contains("html")
            let isTextish = contentType.contains("text/")
                || contentType.contains("json")
                || contentType.contains("xml")
            let rasterExtensions: Set<String> = [
                "bmp", "gif", "heic", "heif", "ico", "jpeg", "jpg",
                "png", "tif", "tiff", "webp",
            ]
            let isRasterImage =
                (contentType.hasPrefix("image/")
                    && !contentType.contains("svg"))
                || rasterExtensions.contains(url.pathExtension.lowercased())
            guard isHTML || isTextish || isRasterImage else {
                DispatchQueue.main.async {
                    completion(.failure(.notHTML(contentType)))
                }
                return
            }

            var html: String
            if isRasterImage {
                // Feed top-level images back through the same <img> path used
                // by normal pages. The engine will fetch and decode the image
                // asynchronously via ImageIO.
                html = Self.wrapAsImage(url: finalURL)
            } else {
                html = Self.decode(
                    data, headerEncoding: response?.textEncodingName,
                    sniffMeta: isHTML)
            }
            if isTextish && !isHTML {
                // Plain text / JSON: present verbatim, monospaced.
                html = Self.wrapAsPre(html, title: finalURL)
            }

            self.fetchStylesheets(html: html, baseURL: finalURL) { sheets in
                completion(.success(Page(
                    html: html, finalURL: finalURL, stylesheets: sheets)))
            }
        }.resume()
    }

    private static func wrapAsPre(_ text: String, title: String) -> String {
        let escaped = text
            .replacingOccurrences(of: "&", with: "&amp;")
            .replacingOccurrences(of: "<", with: "&lt;")
            .replacingOccurrences(of: ">", with: "&gt;")
        return "<html><head><title>\(title)</title></head>"
            + "<body><pre>\(escaped)</pre></body></html>"
    }

    private static func wrapAsImage(url: String) -> String {
        let escaped = url
            .replacingOccurrences(of: "&", with: "&amp;")
            .replacingOccurrences(of: "\"", with: "&quot;")
            .replacingOccurrences(of: "<", with: "&lt;")
            .replacingOccurrences(of: ">", with: "&gt;")
        return "<html><head><title>\(escaped)</title></head>"
            + "<body><img src=\"\(escaped)\" alt=\"\(escaped)\"></body></html>"
    }

    // MARK: - Charset decoding

    /// header charset → <meta> sniff → UTF-8 → Latin-1 (never fails).
    static func decode(_ data: Data, headerEncoding: String?,
                       sniffMeta: Bool) -> String {
        var ianaName = headerEncoding
        if ianaName == nil, sniffMeta {
            ianaName = sniffMetaCharset(data)
        }
        if let name = ianaName {
            let cfEnc = CFStringConvertIANACharSetNameToEncoding(
                name as CFString)
            if cfEnc != kCFStringEncodingInvalidId {
                let nsEnc = CFStringConvertEncodingToNSStringEncoding(cfEnc)
                if let s = String(
                    data: data, encoding: String.Encoding(rawValue: nsEnc)) {
                    return s
                }
            }
        }
        return String(data: data, encoding: .utf8)
            ?? String(data: data, encoding: .isoLatin1)
            ?? ""
    }

    /// Look for <meta charset=...> / http-equiv content-type in the head.
    private static func sniffMetaCharset(_ data: Data) -> String? {
        let head = data.prefix(4096)
        guard let ascii = String(data: head, encoding: .ascii)
            ?? String(data: head, encoding: .isoLatin1) else { return nil }
        let pattern = "charset\\s*=\\s*[\"']?([A-Za-z0-9._-]+)"
        guard let re = try? NSRegularExpression(
            pattern: pattern, options: [.caseInsensitive]) else { return nil }
        let range = NSRange(ascii.startIndex..., in: ascii)
        guard let m = re.firstMatch(in: ascii, range: range),
              let r = Range(m.range(at: 1), in: ascii) else { return nil }
        return String(ascii[r])
    }

    // MARK: - Stylesheet prefetch

    private static let linkRe = try! NSRegularExpression(
        pattern: "<link\\b[^>]*>", options: [.caseInsensitive])
    private static let hrefRe = try! NSRegularExpression(
        pattern: "href\\s*=\\s*(\"([^\"]*)\"|'([^']*)'|([^\\s>]+))",
        options: [.caseInsensitive])
    private static let relStylesheetRe = try! NSRegularExpression(
        pattern: "rel\\s*=\\s*[\"']?\\s*stylesheet",
        options: [.caseInsensitive])
    private static let importRe = try! NSRegularExpression(
        pattern: "@import\\s+(?:url\\(\\s*)?[\"']?([^\"'()\\s;]+)",
        options: [.caseInsensitive])

    private static func matches(_ re: NSRegularExpression, _ s: String)
        -> [[String?]]
    {
        let range = NSRange(s.startIndex..., in: s)
        return re.matches(in: s, range: range).map { m in
            (0..<m.numberOfRanges).map { i in
                Range(m.range(at: i), in: s).map { String(s[$0]) }
            }
        }
    }

    /// Minimal entity decoding for attribute values: the parser sees
    /// entity-decoded hrefs, so cache keys must match that form.
    static func decodeEntities(_ s: String) -> String {
        guard s.contains("&") else { return s }
        return s
            .replacingOccurrences(of: "&amp;", with: "&")
            .replacingOccurrences(of: "&lt;", with: "<")
            .replacingOccurrences(of: "&gt;", with: ">")
            .replacingOccurrences(of: "&quot;", with: "\"")
            .replacingOccurrences(of: "&#39;", with: "'")
            .replacingOccurrences(of: "&#038;", with: "&")
    }

    /// hrefs of <link rel=stylesheet> tags, in document order.
    static func stylesheetHrefs(in html: String) -> [String] {
        matches(linkRe, html).compactMap { m -> String? in
            guard let tag = m[0] else { return nil }
            let tagRange = NSRange(tag.startIndex..., in: tag)
            guard relStylesheetRe.firstMatch(in: tag, range: tagRange) != nil,
                  let href = matches(hrefRe, tag).first
            else { return nil }
            return (href[2] ?? href[3] ?? href[4]).map(decodeEntities)
        }
    }

    private func fetchStylesheets(
        html: String, baseURL: String,
        completion: @escaping ([String: String]) -> Void
    ) {
        var sheets: [String: String] = [:]
        let lock = NSLock()
        let group = DispatchGroup()

        func fetchCSS(_ absURL: String, depth: Int) {
            lock.lock()
            let seen = sheets[absURL] != nil
            if !seen { sheets[absURL] = "" }   // reserve to stop cycles
            lock.unlock()
            guard !seen, depth <= 3, let url = URL(string: absURL) else {
                return
            }

            group.enter()
            session.dataTask(with: url) { data, response, _ in
                defer { group.leave() }
                guard let data = data else { return }
                let css = Self.decode(
                    data, headerEncoding: response?.textEncodingName,
                    sniffMeta: false)
                lock.lock()
                sheets[absURL] = css
                lock.unlock()
                // one level down: @imports of this sheet, relative to it
                for m in Self.matches(Self.importRe, css) {
                    if let href = m[1],
                       let abs = self.resolve(href, absURL) {
                        fetchCSS(abs, depth: depth + 1)
                    }
                }
            }.resume()
        }

        for href in Self.stylesheetHrefs(in: html) {
            if let abs = resolve(href, baseURL) {
                fetchCSS(abs, depth: 0)
            }
        }
        // Inline <style> @imports resolve against the page URL.
        for m in Self.matches(Self.importRe, html) {
            if let href = m[1], let abs = resolve(href, baseURL) {
                fetchCSS(abs, depth: 0)
            }
        }

        group.notify(queue: .main) {
            completion(sheets.filter { !$0.value.isEmpty })
        }
    }
}
