// mutool run annotate.js in.pdf out.pdf "phrase" RRGGBB "comment" ["phrase" RRGGBB "comment" ...]
var doc = Document.openDocument(scriptArgs[0]);
var page = doc.loadPage(0);
for (var i = 2; i + 2 < scriptArgs.length; i += 3) {
  var hits = page.search(scriptArgs[i]);
  if (!hits.length) { print("not found: " + scriptArgs[i]); continue; }
  var c = scriptArgs[i + 1];
  var rgb = [parseInt(c.substring(0, 2), 16) / 255, parseInt(c.substring(2, 4), 16) / 255, parseInt(c.substring(4, 6), 16) / 255];
  var a = page.createAnnotation("Highlight");
  a.setQuadPoints(hits[0]);
  a.setColor(rgb);
  a.setAuthor("C. Wanderer");
  if (scriptArgs[i + 2]) a.setContents(scriptArgs[i + 2]);
  a.update();
}
doc.save(scriptArgs[1]);
