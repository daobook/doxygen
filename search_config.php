<?php

$config = array(
  'PROJECT_NAME' => "Doxygen",
  'GENERATE_TREEVIEW' => true,
  'DISABLE_INDEX' => false,
);

$translator = array(
  'search_results_title' => "搜索结果",
  'search_results' => array(
    0 => "抱歉,未找到与您查询相符的文档.",
    1 => "找到<b>1</b>篇与您查询相符的文档.",
    2 => "找到<b>\$num</b>篇与您查询相符的文档.优先显示最符合的文档.",
  ),
  'search_matches' => "符合的结果:",
  'search' => "搜索",
  'split_bar' => "<div id=\"side-nav\" class=\"ui-resizable side-nav-resizable\">\n  <div id=\"nav-tree\">\n    <div id=\"nav-tree-contents\">\n      <div id=\"nav-sync\" class=\"sync\"></div>\n    </div>\n  </div>\n  <div id=\"splitbar\" style=\"-moz-user-select:none;\" \n       class=\"ui-resizable-handle\">\n  </div>\n</div>\n<script type=\"text/javascript\">\n/* @license magnet:?xt=urn:btih:cf05388f2679ee054f2beb29a391d25f4e673ac3&amp;dn=gpl-2.0.txt GPL-v2 */\n$(document).ready(function(){initNavTree('search.html',''); initResizable(); });\n/* @license-end */\n</script>\n<div id=\"doc-content\">\n",
  'logo' => "生成于 2021年 十一月 27日 星期六 08:41:15 , 为 Doxygen使用 &#160;\n<a href=\"http://www.doxygen.org/index.html\">\n<img class=\"footer\" src=\"doxygen.png\" alt=\"doxygen\"/></a> 1.8.17 ",
);

?>
