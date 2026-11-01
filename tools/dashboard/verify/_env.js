// 公共环境：浏览器路径与产物目录，都可用环境变量覆盖，换机器不用改脚本。
//   EDGE_PATH  指定 msedge.exe；不设则自动在常见安装位置里找一个存在的
//   OUT_DIR    截图/录像产物目录；不设则用 verify/out（自动创建）
const fs = require('fs');
const path = require('path');

const CANDIDATES = [
  'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
  'C:/Program Files/Microsoft/Edge/Application/msedge.exe',
];

const EDGE = process.env.EDGE_PATH || CANDIDATES.find((p) => fs.existsSync(p)) || CANDIDATES[0];
const OUT = process.env.OUT_DIR || path.join(__dirname, 'out');

if (!fs.existsSync(OUT)) fs.mkdirSync(OUT, { recursive: true });

module.exports = { EDGE, OUT };
