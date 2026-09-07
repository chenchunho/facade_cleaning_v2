#!/usr/bin/env node
// ============================================================================
// 前端「靜默失敗」檢查器
//
// 用法：
//   node web_backend/tools/check_console.js web_backend/public_v2/index.html
//   node web_backend/tools/check_console.js web_backend/public/index.html web_backend/public/app.js
//   （不帶參數＝檢查 public_v2/index.html 與 public/{index.html,app.js}）
//
// 🔴 **為什麼需要這支**：這三類錯**不會有任何徵兆**——
//   ① 重複 id       → `getElementById` 只拿得到第一個，第二個永遠不更新
//   ② 寫到不存在的元素 → `txt()`/`bar()` 找不到就 return，**沒有例外、沒有 console 訊息**，
//                       看起來只像「這個功能還沒做」
//   ③ 有 markup 沒人寫 → 元素永遠停在初始值，同樣像「還沒做」
//   2026-09-04 就一次挖出四個「只有一半」的元素。**這類錯只能靠工具抓。**
//
// 🔴 **這支本身有一段前科，所以才進版控**：2026-09-04 寫過功能相同的一支、
//   跑完就丟、沒進 repo，2026-09-07 要用時只能整支重寫。
//   **一個沒進版控的檢查工具，等於下次還要再寫一次。**
//
// 離開碼：0 = 全過，1 = 有問題（可直接串在部署前）。
// ============================================================================
'use strict';
const fs = require('fs');
const path = require('path');

// JS 端「指名一個 id」的所有寫法。新增同類 helper 時要回來補。
const TARGET_PATTERNS = [
  /\btxt\(\s*'([^']+)'/g,
  /\bbar\(\s*'([^']+)'/g,
  /\bcls\(\s*'([^']+)'/g,
  /getElementById\(\s*'([^']+)'\s*\)/g,
  /getElementById\(\s*"([^"]+)"\s*\)/g,
];

// 🔴 **本工具的已知盲區，讀結果前必須知道**：上面全是**字面字串**的比對。
//    用樣板字串動態組出來的 id（`getElementById(`vac-${n}`)`）**看不到**。
//    後果是不對稱的：
//      · ② 會**漏報**（真的寫到不存在的元素，抓不到）—— 這是假陰性，比較危險
//      · ③ 的 ℹ️ 會**誤報**（元素其實有人寫，只是動態的）
//    2026-09-07 首次跑就踩到：8080 的 `vac-5..8` 被列進 ℹ️，實際是 `vac-${id}` 寫的。
//    ⇒ 下面把「動態前綴」抓出來，① 用來消掉 ℹ️ 的誤報，② 用來**如實報出涵蓋率不是 100%**。
//    📌 **一個會靜默漏掉某一類的檢查工具，比沒有工具更危險** —— 它會讓人以為查過了。
const DYNAMIC_PATTERNS = [
  /getElementById\(\s*`([^`$]*)\$\{/g,
  /\btxt\(\s*`([^`$]*)\$\{/g,
  /\bbar\(\s*`([^`$]*)\$\{/g,
  /\bcls\(\s*`([^`$]*)\$\{/g,
];

// 🔴 **按「組」檢查，不是按檔**。console v2 是單檔（markup + JS 同在 index.html），
//    但 8080 那組是 `index.html` + `app.js` 兩個檔 —— 逐檔檢查的話：
//      · index.html 會把 151 個 id 全報成「沒人寫」（JS 不在這個檔裡）
//      · **app.js 寫到 index.html 沒有的 id 這一類根本檢不到**，而那正是 8080 的真實風險
//    所以同一個 GUI 的所有檔要合成一組再比對。
function checkGroup(label, files) {
  let declared = [], usedAll = new Set(), braces = 0, divOpen = 0, divClose = 0;
  const dynPrefixes = new Set();
  const perFile = [];

  for (const file of files) {
    const src = fs.readFileSync(file, 'utf8');
    const isHtml = /\.html?$/i.test(file);
    const ids = [...src.matchAll(/\bid\s*=\s*"([^"]+)"/g)].map(m => m[1]);
    declared = declared.concat(ids);
    for (const p of TARGET_PATTERNS) for (const m of src.matchAll(p)) usedAll.add(m[1]);
    for (const p of DYNAMIC_PATTERNS) for (const m of src.matchAll(p)) if (m[1]) dynPrefixes.add(m[1]);
    braces += (src.match(/{/g) || []).length - (src.match(/}/g) || []).length;
    if (isHtml) {
      divOpen  += (src.match(/<div\b/g) || []).length;
      divClose += (src.match(/<\/div>/g) || []).length;
    }
    perFile.push({ file, src, isHtml });
  }

  const declaredSet = new Set(declared);
  const dupes = [...new Set(declared.filter((v, i) => declared.indexOf(v) !== i))];
  const writtenButMissing = [...usedAll].filter(id => !declaredSet.has(id));

  const rows = [
    ['① 重複 id',          dupes.length === 0,             dupes.join(', ')],
    ['② 寫到不存在的元素',  writtenButMissing.length === 0, writtenButMissing.join(', ')],
    ['③ 大括號平衡',       braces === 0,                   '差 ' + braces],
    ['④ <div> 開合',       divOpen === divClose,           divOpen + '/' + divClose],
  ];

  console.log('── ' + label + '  (' + files.map(f => path.basename(f)).join(' + ') + ')');
  console.log('   宣告的 id ' + declared.length + ' · JS 指名的 id ' + usedAll.size);
  if (dynPrefixes.size) {
    // 如實報出來：這幾個前綴底下的元素本工具查不到，② 的涵蓋率不是 100%。
    console.log('   ⚠️ 另有樣板字串動態組的 id（本工具看不到，② 涵蓋率非 100%）: '
                + [...dynPrefixes].map(x => '`' + x + '${…}`').join(', '));
  }
  let ok = true;
  for (const [name, pass, detail] of rows) {
    if (!pass) ok = false;
    console.log('   ' + name.padEnd(20) + (pass ? '✅' : '❌ ' + detail));
  }

  // ③ 的另一半：宣告了、而且**全組只出現這一次** —— 只列出來，不當成失敗。
  // 容器、純靜態文字、用 querySelector / 事件委派抓的都會落在這裡，誤報率高，
  // 但值得看一眼：2026-09-07 就是這樣抓到 `polln` 那行寫死的輪詢說明少了 `rail 7s`。
  const countAll = id => perFile.reduce((n, f) =>
    n + (f.src.match(new RegExp(id.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'), 'g')) || []).length, 0);
  // 落在某個動態前綴底下的不算「沒人寫」—— 它只是被動態寫的。
  const coveredByDynamic = id => [...dynPrefixes].some(pre => pre && id.startsWith(pre));
  const lonely = declared.filter(id => !usedAll.has(id) && !coveredByDynamic(id) && countAll(id) <= 1);
  if (lonely.length) {
    console.log('   ℹ️ 宣告了但全組只出現一次（純 markup，值得看一眼）: ' + lonely.join(', '));
  }
  return ok;
}

const args = process.argv.slice(2);
const repo = path.resolve(__dirname, '..', '..');
const groups = args.length
  ? [['指定檔案', args]]
  : [
      ['console v2 (8081)', [path.join(repo, 'web_backend/public_v2/index.html')]],
      ['現行 GUI (8080)',   [path.join(repo, 'web_backend/public/index.html'),
                             path.join(repo, 'web_backend/public/app.js')]],
    ];

let allOk = true;
for (const [label, files] of groups) {
  const missing = files.filter(f => !fs.existsSync(f));
  if (missing.length) { console.log('── ' + label + '\n   ❌ 找不到: ' + missing.join(', ')); allOk = false; continue; }
  if (!checkGroup(label, files)) allOk = false;
  console.log('');
}
console.log(allOk ? '全部通過。' : '有項目未通過。');
process.exit(allOk ? 0 : 1);
