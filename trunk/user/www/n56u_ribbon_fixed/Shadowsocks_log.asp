<!DOCTYPE html>
<html>
<head>
<title></title>
<meta http-equiv="Content-Type" content="text/html; charset=utf-8">
<meta http-equiv="Pragma" content="no-cache">
<meta http-equiv="Expires" content="-1">

<link rel="shortcut icon" href="images/favicon.ico">
<link rel="icon" href="images/favicon.png">
<link rel="stylesheet" type="text/css" href="/bootstrap/css/bootstrap.min.css">
<link rel="stylesheet" type="text/css" href="/bootstrap/css/main.css">
<link rel="stylesheet" type="text/css" href="/bootstrap/css/engage.itoggle.css">

<script type="text/javascript" src="/jquery.js"></script>
<script type="text/javascript" src="/bootstrap/js/bootstrap.min.js"></script>
<script type="text/javascript" src="/bootstrap/js/engage.itoggle.min.js"></script>
<script type="text/javascript" src="/state.js"></script>
<script type="text/javascript" src="/general.js"></script>
<script type="text/javascript" src="/itoggle.js"></script>
<script type="text/javascript" src="/popup.js"></script>
<script type="text/javascript" src="/help.js"></script>
<style>
.load {
    display: flex;
    justify-content: center; /* 水平居中 */
    align-items: center; /* 垂直居中 */
    height: 432px; /* 设置容器高度 */
}
.loader {
  width: 50px;
  aspect-ratio: 1;
  display: grid;
  color: #6d51a4;
  background: radial-gradient(farthest-side, currentColor calc(100% - 6px),#0000 calc(100% - 5px) 0);
  -webkit-mask: radial-gradient(farthest-side,#0000 calc(100% - 13px),#000 calc(100% - 12px));
  border-radius: 50%;
  animation: l19 2s infinite linear;
}
.loader::before,
.loader::after {    
  content: "";
  grid-area: 1/1;
  background:
    linear-gradient(currentColor 0 0) center,
    linear-gradient(currentColor 0 0) center;
  background-size: 100% 10px,10px 100%;
  background-repeat: no-repeat;
}
.loader::after {
   transform: rotate(45deg);
}

@keyframes l19 { 
  100%{transform: rotate(1turn)}
}

body.body_iframe{
    margin: 0;
}
.nav {
    margin-bottom: 0px;
}
</style>
<script>
var $j = jQuery.noConflict();
var url = "/ss_link.asp";

function get_sslink(){
	$j.ajax({
		url: url,
		dataType: 'json',
		cache: true,
		error: function(xhr){
			;
		},
		success: function(data){
			$j('.load').remove();
			$j.each(data, function(index, item) {
				var t_body = '';
				var proto, name, hostname, port, password;
				proto = item.proto;
				if (proto == "ss") {
					name = decodeURIComponent(item.name);
					hostname = item.server;
					password = item.password;
					port = item.port;
				}
				t_body += '<tr>\n';
				t_body += '  <td>'+proto+'</td>\n';
				t_body += '  <td>🔗'+name+'</td>\n';
				t_body += '  <td>'+hostname+'</td>\n';
				t_body += '  <td>'+password+'</td>\n';
				t_body += '  <td>'+port+'</td>\n';
				t_body += '</tr>\n';
				$j('#ss_table').append(t_body);
			});
			parent.adjustIframeHeight();
		}
	});
}

$j(document).ready(function(){
	get_sslink();
});

function initial(){
}
</script>

<style>
.nav-tabs > li > a {
    padding-right: 6px;
    padding-left: 6px;
}

th, td {
    border: 1px solid #4e4040; /* 定义单元格的边框样式 */
    padding: 8px; /* 设置单元格内边距 */
    text-align: center; /* 居中内容 */
}

th {
    background-color: #35254b; /* 表头背景色 */
}
</style>
</head>

<body class="body_iframe" onload="initial();" onunLoad="return unload_body();">
<ul class="nav nav-tabs">
    <li><a href="/Shadowsocks_conf.asp"><#menu5_1_1#></a></li>
    <li class="active"><a href="javascript:;"><#menu5_16_20#></a></li>
</ul>
<table width="100%" cellpadding="4" cellspacing="0" class="table" id="ss_table">
<tr> <th colspan="5" style="background-color: rgba(171, 168, 167,0.2);"><#menu5_16_32#></th> </tr>
<tr>
        <th width="10%"><#ss_proto#></th>
        <th width="23%"><#ss_name#></th>
        <th width="15%"><#ss_server#></th>
      	<th width="44%" style="text-align: center"><#AiDisk_Password#></th>
        <th width="10%"><#ss_port#></th>
</tr>
</table>
<div class="load">
    <div class="loader"></div>
</div>
</body>
</html>
