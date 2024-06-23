<!DOCTYPE html>
<html>
<head>
<title><#Web_Title#> - <#menu5_3_5#></title>
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
<script type="text/javascript" src="/client_function.js"></script>
<script>
var $j = jQuery.noConflict();

$j(document).ready(function() {
	init_itoggle('ddnsto_enable', change_ddnsto_enabled);
});
</script>

<script>
var isMenuopen = 0;

function initial(){
	var id_menu = 6;
	if(!support_ipv6())
		id_menu--;

	show_banner(1);
	show_menu(5,4,id_menu);
	show_footer();

	load_body();
	change_ddnsto_enabled();
}

function applyRule(){
	if(validForm()){
		showLoading();
		document.form.action_mode.value = " Apply ";
		document.form.current_page.value = "/Advanced_DDNSClient_Content.asp";
		document.form.next_page.value = "";
		document.form.submit();
	}
}

function validForm(){
	return true;
}

function change_ddnsto_enabled(){
	var v = document.form.ddnsto_enable[0].checked;
	showhide_div("row_ddnsto_token", v);
}

function hideClients_Block(){
	$j("#chevron").children('i').removeClass('icon-chevron-up').addClass('icon-chevron-down');
	$('ClientList_Block').style.display='none';
	isMenuopen = 0;
}

function done_validating(action){
	refreshpage();
}
</script>
</head>
<body onload="initial();" onunLoad="return unload_body();">

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
    <form method="post" name="form" id="ruleForm" action="/start_apply.htm" target="hidden_frame">
    <input type="hidden" name="current_page" value="Advanced_DDNSClient_Content.asp">
    <input type="hidden" name="next_page" value="">
    <input type="hidden" name="next_host" value="">
    <input type="hidden" name="sid_list" value="DDNSTO;">
    <input type="hidden" name="group_id" value="">
    <input type="hidden" name="action_mode" value="">
    <input type="hidden" name="action_script" value="">

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
                            <h2 class="box_head round_top"><#menu5_3#> - <#menu5_3_7#></h2>
                            <div class="round_bottom">
                                <div class="row-fluid">
                                    <div id="tabMenu" class="submenuBlock"></div>
                                    <table width="100%" cellpadding="4" cellspacing="0" class="table">
                                        <tr>
                                            <td colspan="2"><div class="alert alert-info" style="margin: 2px;"><#DDNSTO_sectiondesc#></div></td>
                                        </tr>
                                        <tr>
                                            <th width="50%"><#DDNSTO_itemname#></a></th>
                                            <td>
                                                <div class="main_itoggle">
                                                    <div id="ddnsto_enable_on_of">
                                                        <input type="checkbox" id="ddnsto_enable_fake" <% nvram_match_x("", "ddnsto_enable", "1", "value=1 checked"); %><% nvram_match_x("", "ddnsto_enable", "0", "value=0"); %> />
                                                    </div>
                                                </div>

                                                <div style="position: absolute; margin-left: -10000px;">
                                                    <input type="radio" value="1" name="ddnsto_enable" id="ddnsto_enable_1" onclick="change_ddnsto_enabled();" <% nvram_match_x("", "ddnsto_enable", "1", "checked"); %>/><#checkbox_Yes#>
                                                    <input type="radio" value="0" name="ddnsto_enable" id="ddnsto_enable_0" onclick="change_ddnsto_enabled();" <% nvram_match_x("", "ddnsto_enable", "0", "checked"); %>/><#checkbox_No#>
                                                </div>
                                            </td>
                                        </tr>
					<tr id="row_ddnsto_token">
                                            <th><#DDNSTO_token#></th>
                                            <td>
						<input type="password" class="input" maxlength="64" size="48" name="ddnsto_token" id="ddnsto_token" value="<% nvram_get_x("","ddnsto_token"); %>" onKeyPress="return is_string(this,event);" />
						<button style="margin-left: -5px;" class="btn" type="button" onclick="passwordShowHide('ddnsto_token')"><i class="icon-eye-close"></i></button>
                                            </td>
                                        </tr>
                                    </table>

                                    <table class="table">
                                        <tr>
                                            <td style="border: 0 none;"><center><input name="button" type="button" class="btn btn-primary" style="width: 219px" onclick="applyRule();" value="<#CTL_apply#>"/></center></td>
                                        </tr>
                                    </table>
                                </div>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        </div>
    </div>

    </form>

    <div id="footer"></div>
</div>
</body>
</html>
