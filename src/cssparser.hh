#ifndef __CSSPARSER_HH__
#define __CSSPARSER_HH__

#include "css.hh"

class DilloHtml;

class CssParser {
   public:
      /** Three-valued result of a media query. UNKNOWN is a feature
       *  Dillo does not evaluate (width, resolution, ...); callers keep
       *  their historical behaviour for it. */
      typedef enum {
         MEDIA_FALSE, MEDIA_TRUE, MEDIA_UNKNOWN
      } MediaMatch;

   private:
      static bool prefersDark;
      static unsigned colorSchemeQueries;

      typedef enum {
         CSS_TK_DECINT, CSS_TK_FLOAT, CSS_TK_COLOR, CSS_TK_SYMBOL,
         CSS_TK_STRING, CSS_TK_CHAR, CSS_TK_END
      } CssTokenType;

      static const int maxStrLen = 256;
      CssContext *context;
      CssOrigin origin;
      const DilloUrl *baseUrl;

      const char *buf;
      int buflen, bufptr;

      CssTokenType ttype;
      char tval[maxStrLen];
      bool withinBlock;
      bool spaceSeparated; /* used when parsing CSS selectors */

      CssParser(CssContext *context, CssOrigin origin, const DilloUrl *baseUrl,
                const char *buf, int buflen);
      int getChar();
      void ungetChar();
      void nextToken();
      bool skipString(int c, const char *string);
      bool tokenMatchesProperty(CssPropertyName prop, CssValueType * type);
      bool parseValue(CssPropertyName prop, CssValueType type,
                      CssPropertyValue * val);
      bool parseWeight();
      bool parseRgbColorComponent(int32_t *cc, int *percentage);
      bool parseRgbColor(int32_t *c);
      void parseDeclaration(CssPropertyList * props,
                            CssPropertyList * importantProps);
      bool parseSimpleSelector(CssSimpleSelector *selector);
      char *parseUrl();
      void parseImport(DilloHtml *html);
      void parseMedia();
      MediaMatch parseMediaQueryList();
      MediaMatch parseMediaQuery();
      MediaMatch parseMediaCondition();
      MediaMatch parseMediaInParens();
      MediaMatch evalMediaFeature(const char *name, const char *value);
      void skipMediaQuery();
      CssSelector *parseSelector();
      void parseRuleset();
      void ignoreBlock();
      void ignoreStatement();

   public:
      static void parseDeclarationBlock(const DilloUrl *baseUrl,
                                        const char *buf, int buflen,
                                        CssPropertyList *props,
                                        CssPropertyList *propsImortant);
      static void parse(DilloHtml *html, const DilloUrl *baseUrl, CssContext *context,
                        const char *buf, int buflen, CssOrigin origin);
      static const char *propertyNameString(CssPropertyName name);

      /** Evaluate a media query list such as an HTML media attribute. */
      static MediaMatch matchMedia(const char *mediaList);
      /** The value prefers-color-scheme reports; set by the platform. */
      static void setPrefersDark(bool dark) { prefersDark = dark; }
      static bool getPrefersDark() { return prefersDark; }
      /** Bumped whenever a query asks about prefers-color-scheme, so a
       *  caller can tell whether what it parsed depends on the scheme. */
      static unsigned getColorSchemeQueries() { return colorSchemeQueries; }
};

#endif
