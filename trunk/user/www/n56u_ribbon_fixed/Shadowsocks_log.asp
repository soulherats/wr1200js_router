<!DOCTYPE html>
<html>
<head>
<title><#Web_Title#> - <#menu5_16#></title>
<meta http-equiv="Content-Type" content="text/html; charset=utf-8">
<meta http-equiv="Pragma" content="no-cache">
<meta http-equiv="Expires" content="-1">

<link rel="shortcut icon" href="images/favicon.ico">
<link rel="icon" href="images/favicon.png">
<link rel="stylesheet" type="text/css" href="/bootstrap/css/bootstrap.min.css">
<link rel="stylesheet" type="text/css" href="/bootstrap/css/main.css">

<script type="text/javascript" src="/jquery.js"></script>
<script type="text/javascript" src="/bootstrap/js/bootstrap.min.js"></script>
<script type="text/javascript" src="/state.js"></script>
<script type="text/javascript" src="/popup.js"></script>

<script>
var $j = jQuery.noConflict();
var url = "/ss_link.asp";

function initial(){
  show_banner(2);
  show_menu(5,13,2);
  show_footer();
}

function copyToClipboard(text) {
    var textField = document.createElement('textarea');
    textField.innerText = text;
    document.body.appendChild(textField);
    textField.select();
    try {
        document.execCommand('copy');
        console.log('已复制到剪贴板: ' + text);
    } catch (err) {
        console.error('无法复制', err);
    }
    textField.remove();
}

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
                                        name = decodeURIComponent(atob(item.name));
                                        
                                        if (!/[\u{1F1E6}-\u{1F1FF}]{2}/u.test(name)) {
                                                var countryMatch = name.match(/[A-Z]{2}/);
                                                if (countryMatch) {
                                                        var code = countryMatch[0];
                                                        var flag = String.fromCodePoint(...code.split('').map(c => c.charCodeAt() + 127397));
                                                        var pattern = new RegExp(code + "-?");
                                                        name = flag + " " + name.replace(pattern, "").trim();
                                                } else {
                                                        name = "🚀 " + name; 
                                                }
                                        }
                                        
                                        hostname = item.server;
                                        password = item.password;
                                        port = item.port;
                                }
                                t_body += '<tr>\n';
                                t_body += '  <td>'+proto+'</td>\n';
                                t_body += '  <td>'+name+'</td>\n';
                                t_body += '  <td title="' + hostname + '" oncontextmenu="copyToClipboard(\'' + hostname + '\'); return false;">' + hostname + '</td>\n';
                                t_body += '  <td title="' + password + '" oncontextmenu="copyToClipboard(\'' + password + '\'); return false;">' + password + '</td>\n';
                                t_body += '  <td>'+port+'</td>\n';
                                t_body += '</tr>\n';
                                $j('#ss_table').append(t_body);
                        });
                }
        });
}

$j(document).ready(function(){
	get_sslink();
});
</script>
 
<style>
.nav-tabs > li > a {
    padding-right: 6px;
    padding-left: 6px;
}

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

#ss_table {
    table-layout: auto; /* 改为自动布局，优先撑开内容 */
    width: 100%;
}

#ss_table th, #ss_table td {
    border: 1px solid #4e4040;
    padding: 8px;
    text-align: center;
    vertical-align: middle;
    white-space: nowrap; /* 默认不换行 */
}

#ss_table td:nth-child(3),
#ss_table td:nth-child(4) {
    max-width: 120px;       /* 默认最大宽度，可根据需要调整 */
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    cursor: pointer;        /* 鼠标移上去显示手指形状，提示可交互 */
    transition: all 0.2s ease-in-out; /* 平滑过渡效果 */
}

#ss_table td:nth-child(3),
#ss_table td:nth-child(4) {
    max-width: 150px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
}
</style>
</head>

<body onload="initial();">
  <div class="wrapper">
    <div class="container-fluid" style="padding-right: 0px">
        <div class="row-fluid">
            <div class="span3"><center><div id="logo"></div></center></div>
            <div class="span9" >
                <div id="TopBanner"></div>
            </div>
        </div>
    </div>

    <div id="Loading" class="popup_bg"></div>

    <iframe name="hidden_frame" id="hidden_frame" src="" width="0" height="0" frameborder="0"></iframe>

    <div class="container-fluid">
      <div class="row-fluid">
        <div class="span3">
          <!--Sidebar content-->
          <!--=====Beginning of Main Menu=====-->
          <div class="well sidebar-nav side_nav" style="padding: 0px;">
            <ul id="mainMenu" class="clearfix"></ul>
            <ul class="clearfix">
              <li>
                <div id="subMenu" class="accordion"></div>
              </li>
            </ul>
          </div>
        </div>
    
        <div class="span9">
          <!--Body content-->
          <div class="row-fluid">
            <div class="span12">
              <div class="box well grad_colour_dark_blue">
                <h2 class="box_head round_top"><#menu5_16#></h2>
                <div class="round_bottom">
                  <div class="row-fluid">
                    <div id="tabMenu" class="submenuBlock"></div>
                    <table width="100%" cellpadding="4" cellspacing="0" class="table" id="ss_table">
                      <tr>
                        <th colspan="5" style="background-color: rgba(171, 168, 167,0.2);">
                          <#menu5_16_32#>
                        </th>
                      </tr>
                      <tr>
                        <th><#ss_proto#></th>
                        <th><#ss_name#></th>
                        <th><#ss_server#></th>
                        <th><#AiDisk_Password#></th>
                        <th><#ss_port#></th>
                      </tr>
                    </table>
                    <div class="load">
                      <div class="loader"></div>
                    </div>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
    </div>
    <div id="footer"></div>
</body>
</html>
