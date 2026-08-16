<!DOCTYPE html>
<html>
<head>
<title><#Web_Title#> - <#menu4_2#> : <#menu4_2_2#></title>
<meta http-equiv="Content-Type" content="text/html; charset=utf-8">
<meta http-equiv="Pragma" content="no-cache">
<meta http-equiv="Expires" content="-1">

<link rel="shortcut icon" href="images/favicon.ico">
<link rel="icon" href="images/favicon.png">
<link rel="stylesheet" type="text/css" href="/bootstrap/css/bootstrap.min.css">
<link rel="stylesheet" type="text/css" href="/bootstrap/css/main.css">

<script type="text/javascript" src="/jquery.js"></script>
<script type="text/javascript" src="/bootstrap/js/bootstrap.min.js"></script>
<script type="text/javascript" src="/bootstrap/js/highcharts.js"></script>
<script type="text/javascript" src="/bootstrap/js/highcharts_theme.js"></script>
<script type="text/javascript" src="/net_speed_tabs.js"></script>
<script type="text/javascript" src="/state.js"></script>
<script type="text/javascript" src="/popup.js"></script>
<script>
var $j = jQuery.noConflict();
<% bandwidth("speed", ""); %>
var netChart;
var cprefix = "bw_24_";

var net_chart_24 = {
    chart: {
        renderTo: 'net_chart',
        zoomType: 'x',
        spacingRight: 15,
        height: 420
    },
    title : {
        text: 'Wired: LAN',
        align: 'center'
    },
    xAxis: {
        type: 'datetime',
        minRange: 300*1000,
        title: {
            text: null
        }
    },
    yAxis: {
        title: {
            text: '<#HSTOCK_Bandwidth#>'
        },
        min: 0,
        minRange: 0.04,
        opposite: false,
        startOnTick: false,
        showFirstLabel: false
    },
    plotOptions: {
        series: {
            animation: false
        },
        areaspline: {
            lineWidth: 1,
            fillOpacity: 0.3
        }
    },
    legend: {
        enabled: true,
        verticalAlign: 'top',
        floating: true,
        align: 'right'
    },
    rangeSelector: {
        buttons: [{
            count: 1,
            type: 'hour',
            text: '1H'
        },{
            count: 3,
            type: 'hour',
            text: '3H'
        },{
            count: 6,
            type: 'hour',
            text: '6H'
        },{
            count: 12,
            type: 'hour',
            text: '12H'
        },{
            type: 'all',
            text: 'All'
        }],
        inputEnabled: false,
        selected: 4
    },
    tooltip:{
        xDateFormat: '%H:%M:%S',
        valueSuffix: ' Mbps',
        valueDecimals: 2
    },
    series: [{
        type: 'areaspline',
        name: '<#Downlink#>',
        color: '#FF9000',
        gapSize: 5,
        threshold: null,
        data: (prepare_array_chart)(0)
    },{
        type: 'areaspline',
        name: '<#Uplink#>',
        color: '#3D7DFF',
        gapSize: 5,
        threshold: null,
        data: (prepare_array_chart)(1)
    }]
};

Highcharts.setOptions(Highcharts.locale);

/* 深色仪表盘主题 */
Highcharts.setOptions({
    chart: {
        backgroundColor: 'rgba(0,0,0,0)',
        plotBackgroundColor: 'rgba(0,0,0,0)',
        borderWidth: 0,
        borderRadius: 0,
        plotBorderWidth: 0,
        plotShadow: false,
        style: { fontFamily: 'Inter, sans-serif' }
    },
    colors: ['#FF9000', '#3D7DFF', '#20A098', '#B0A8C9'],
    title: { style: { color: '#E8E4F2', fontWeight: '600', fontSize: '14px' } },
    subtitle: { style: { color: '#8B84A3', fontSize: '12px' } },
    xAxis: {
        gridLineWidth: 0,
        lineColor: 'rgba(255,255,255,0.15)',
        tickColor: 'rgba(255,255,255,0.18)',
        labels: { style: { color: '#8B84A3', fontSize: '11px' } },
        title: { style: { color: '#8B84A3', fontSize: '11px' } }
    },
    yAxis: {
        gridLineColor: 'rgba(255,255,255,0.055)',
        gridLineWidth: 1,
        minorGridLineWidth: 0,
        lineColor: 'rgba(255,255,255,0.15)',
        tickWidth: 0,
        labels: { style: { color: '#8B84A3', fontSize: '11px' } },
        title: { style: { color: '#8B84A3', fontSize: '11px' } }
    },
    legend: {
        backgroundColor: 'rgba(0,0,0,0)',
        itemStyle: { color: '#C9C3DB', fontSize: '12px' },
        itemHoverStyle: { color: '#FFFFFF' },
        itemHiddenStyle: { color: '#5A5470' }
    },
    tooltip: {
        backgroundColor: 'rgba(24,21,38,0.96)',
        borderColor: 'rgba(255,255,255,0.16)',
        borderRadius: 5,
        borderWidth: 1,
        style: { color: '#E8E4F2', fontSize: '12px' },
        crosshairs: { color: 'rgba(255,255,255,0.25)' }
    },
    rangeSelector: {
        buttonTheme: {
            fill: 'rgba(255,255,255,0.045)',
            stroke: 'rgba(255,255,255,0.14)',
            'stroke-width': 1,
            r: 4,
            style: { color: '#B0A8C9', fontSize: '11px', fontWeight: '400' },
            states: {
                hover: { fill: 'rgba(255,255,255,0.10)', stroke: 'rgba(255,255,255,0.25)', style: { color: '#E8E4F2' } },
                select: { fill: 'rgba(32,160,152,0.22)', stroke: '#20A098', style: { color: '#FFFFFF', fontWeight: '600' } }
            }
        },
        inputStyle: { color: '#E8E4F2', backgroundColor: 'rgba(255,255,255,0.05)', fontSize: '11px' },
        labelStyle: { color: '#B0A8C9' },
        inputBoxBorderColor: 'rgba(255,255,255,0.18)',
        inputBoxWidth: 92
    },
    navigator: {
        outlineColor: 'rgba(255,255,255,0.10)',
        maskFill: 'rgba(255,255,255,0.06)',
        handles: {
            backgroundColor: '#1A1826',
            borderColor: 'rgba(255,255,255,0.25)'
        },
        xAxis: { gridLineColor: 'rgba(255,255,255,0.05)', labels: { style: { color: '#8B84A3' } } }
    },
    scrollbar: {
        barBackgroundColor: 'rgba(255,255,255,0.06)',
        barBorderColor: 'rgba(255,255,255,0.14)',
        buttonBackgroundColor: 'rgba(255,255,255,0.05)',
        buttonBorderColor: 'rgba(255,255,255,0.14)',
        buttonArrowColor: '#B0A8C9',
        rifleColor: 'rgba(255,255,255,0.14)',
        trackBackgroundColor: 'rgba(255,255,255,0.03)',
        trackBorderColor: 'rgba(255,255,255,0.08)'
    },
    credits: { enabled: false }
});

$j(document).ready(function(){
	$j("#tabs a").click(function(){
		switchPage(this.id);
		return false;
	});
	if(get_ap_mode()){
		$j("#tab_tr_dy").parents('li').hide();
		$j("#tab_tr_mo").parents('li').hide();
	}
	netChart = new Highcharts.StockChart(net_chart_24);
});

function initial(){
	show_banner(0);
	show_menu(5, -1, 0);
	show_footer();

	initTab();
	processTabs();
	updateTab(speed_history[netdev]);
	setChartTitle(netdev);

	invoke_timer(poll_next);
}

function getNextTime(){
	var x = (new Date()).getTime();
	return parseInt(x/1000)*1000-(data_period-poll_next)*1000;
}

function redraw_speed(){
	var x = getNextTime();

	for(var i in speed_history){
		var h = speed_history[i];
		if ((typeof(h.rx) === 'undefined') || (typeof(h.tx) === 'undefined'))
			continue;
		
		netChart.series[0].setData(prepareData(x, h.rx), false);
		netChart.series[1].setData(prepareData(x, h.tx), false);
		
		if (netdev !== i){
			netdev = i;
			setChartTitle(i);
			E('sel_netif').value = 'speed-tab-' + i;
		}
		
		break;
	}

	processTabs();
	updateTab(speed_history[netdev]);

	invoke_timer(poll_next);

	netChart.redraw();
}

function eval_netdevs(response){
	speed_history = {};

	try {
		eval(response);
	}
	catch (ex) {
		speed_history = {};
	}

	redraw_speed();
}

function load_netdevs(){
	clearTimeout(idTimerPoll);
	$j.ajax({
		type: "get",
		url: "/update.cgi",
		data: {
			output: "bandwidth",
			arg0: "speed",
			arg1: netdev
		},
		dataType: "script",
		cache: true,
		error: function(xhr){
			invoke_timer(5);
		},
		success: function(response){
			eval_netdevs(response);
		}
	});
}

function prepareData(x,data){
	var newData = [];
	var i, j = 0, p = data_period;
	for(i=(data.length-1); i >= 0; i--)
		newData.unshift([(x - (j++) * p * 1000), (data[i]*8/1000000)]);
	return newData;
}

function prepare_array_chart(id){
	var x = getNextTime();
	var h = speed_history[netdev];
	if (h !== undefined){
		if(id==1){
			if (typeof(h.tx) !== 'undefined')
				return prepareData(x, h.tx);
		}else{
			if (typeof(h.rx) !== 'undefined')
				return prepareData(x, h.rx);
		}
	}

	var data = [], p = 1-parseInt(86400/data_period);
	for(i = p; i <= 0; i++)
		data.push([x+i*data_period*1000, 0]);
	return data;
}

function setChartTitle(ifdesc){
	var title = getTabDesc(ifdesc);
	if (title)
		netChart.setTitle({text: title}, null, false);
}

function setChartData(ifdesc){
	if (!ifdesc)
		return false;
	if(netdev != ifdesc){
		netdev = ifdesc;
		setChartTitle(ifdesc);
		load_netdevs();
	}
}

function handleTabs(arrTabs){
	var o = E('sel_netif');
	var tabName = 'speed-tab-' + netdev;
	free_options(o);
	for(var i=0; i<arrTabs.length; i++)
		add_option(o, arrTabs[i][1], arrTabs[i][0], arrTabs[i][0] === tabName);
}

function tabSelect(tabName){
	var ifdesc = tabName.replace('speed-tab-', '');
	setChartData(ifdesc);
}

function switchPage(id){
	if(id == "tab_bw_rt")
		location.href = "/Main_TrafficMonitor_realtime.asp";
	else if(id == "tab_tr_dy")
		location.href = "/Main_TrafficMonitor_daily.asp#DY";
	else if(id == "tab_tr_mo")
		location.href = "/Main_TrafficMonitor_daily.asp#MO";
	return false;
}

</script>
<style>
/* ==== 深色工业风: 最近24小时流量 ==== */
#tabs {margin-bottom: 0px;}

/* 工具栏: 左侧页面 Tab + 右侧网卡选择 */
.tm-toolbar {
    display: flex;
    justify-content: space-between;
    align-items: center;
    flex-wrap: wrap;
    gap: 6px;
    margin: 0 0 10px 0;
    padding: 0 2px;
}
.tm-toolbar .nav-tabs {border-bottom: 1px solid rgba(255,255,255,0.10); flex: 1 1 auto;}
.tm-toolbar .nav-tabs > li > a {
    color: #9A93B5;
    font-size: 12px;
    letter-spacing: 0.5px;
    padding: 6px 12px;
    border: none;
    background: transparent;
}
.tm-toolbar .nav-tabs > li > a:hover {
    color: #E8E4F2;
    background: rgba(255,255,255,0.04);
    border: none;
}
.tm-toolbar .nav-tabs > .active > a,
.tm-toolbar .nav-tabs > .active > a:hover {
    color: #E8E4F2;
    background: transparent;
    border: none;
    border-bottom: 2px solid #20A098;
    font-weight: 600;
}

/* 网卡下拉: 深色浮层 */
select#sel_netif {
    width: 220px;
    margin: 0;
    padding: 5px 8px;
    background-color: #1A1826;
    color: #E8E4F2;
    border: 1px solid rgba(255,255,255,0.15);
    border-radius: 4px;
    font-size: 12px;
    outline: none;
}
select#sel_netif:hover {border-color: rgba(32,160,152,0.6);}
select#sel_netif:focus {border-color: #20A098;}
select#sel_netif option {
    background-color: #1A1826;
    color: #E8E4F2;
}

/* 图表卡片 */
.tm-chart {
    width: 100%;
    max-width: 760px;
    margin: 0 auto;
    background: rgba(255,255,255,0.025);
    border: 1px solid rgba(255,255,255,0.07);
    border-radius: 8px;
    padding: 10px 12px 4px 8px;
    box-sizing: border-box;
}

/* 统计表: 卡片化 */
.tm-panel {
    margin: 10px auto 4px;
    max-width: 760px;
}
table.table-stat {
    background: rgba(255,255,255,0.025);
    border: 1px solid rgba(255,255,255,0.07);
    border-radius: 8px;
    margin-bottom: 0;
}
.table-stat td {padding: 7px 10px;}
.table-stat th {
    color: #B0A8C9;
    font-size: 12px;
    font-weight: 500;
    letter-spacing: 1px;
    padding: 9px 10px;
    border-bottom: 1px solid rgba(255,255,255,0.10);
    text-align: right;
    background: transparent;
}
.table-stat th:nth-child(2) {text-align: left;}
.table-stat th:first-child {text-align: center;}
.table-stat td {
    border-top: 1px solid rgba(255,255,255,0.05);
    color: #C9C3DB;
    font-size: 13px;
    font-variant-numeric: tabular-nums;
    text-align: right;
    white-space: nowrap;
}
.table-stat td:nth-child(2) {
    text-align: left;
    color: #E8E4F2;
    font-weight: 500;
}
.table-stat td:first-child {text-align: center;}
.table-stat tbody tr:hover {background: rgba(255,255,255,0.045);}
/* 当前值强调 */
.table-stat td .tm-current {
    font-weight: 600;
    color: #FFFFFF;
    font-size: 14px;
}
/* 色标胶囊 (覆盖共享 JS 的 inline 颜色) */
.tm-swatch {
    display: inline-block;
    width: 52px;
    height: 12px;
    border-radius: 6px;
    margin: 0;
    float: none;
    background: #FF9000 !important;
}
.tm-swatch.tm-tx {background: #3D7DFF !important;}

/* navigator 时间轴缩放区: 迷你系列压成低调灰, 遮罩近透明 */
.highcharts-navigator-series path,
#highcharts-navigator-series .highcharts-area,
.highcharts-navigator .highcharts-area,
.highcharts-navigator-series .highcharts-area {
    stroke: rgba(255,255,255,0.28) !important;
    fill: rgba(255,255,255,0.05) !important;
}
.highcharts-navigator-series .highcharts-graph {
    stroke: rgba(255,255,255,0.28) !important;
}
.highcharts-navigator-mask,
.highcharts-navigator rect[fill*="128,179,236"],
.highcharts-navigator rect[fill*="128, 179, 236"] {
    fill: rgba(255,255,255,0.04) !important;
}
.highcharts-navigator .highcharts-navigator-handle rect,
.highcharts-navigator-handle rect {
    fill: #1A1826 !important;
    stroke: rgba(255,255,255,0.30) !important;
}
.highcharts-navigator-outline {
    stroke: rgba(255,255,255,0.10) !important;
}
</style>
</head>

<body onload="initial();" >

<div class="wrapper">
    <div class="container-fluid" style="padding-right: 0px">
        <div class="row-fluid">
            <div class="span3"><center><div id="logo"></div></center></div>
            <div class="span9">
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
                            <h2 class="box_head round_top"><#menu4#> - <#menu4_2_2#></h2>
                            <div class="round_bottom">
                                <div class="row-fluid">
                                    <div id="tabMenu" class="submenuBlock"></div>

                                    <div class="tm-toolbar">
                                        <ul id="tabs" class="nav nav-tabs">
                                            <li><a href="javascript:void(0)" id="tab_bw_rt"><#menu4_2_1#></a></li>
                                            <li class="active"><a href="javascript:void(0)" id="tab_bw_24"><#menu4_2_2#></a></li>
                                            <li><a href="javascript:void(0)" id="tab_tr_dy"><#menu4_2_3#></a></li>
                                            <li><a href="javascript:void(0)" id="tab_tr_mo"><#menu4_2_4#></a></li>
                                        </ul>
                                        <select id="sel_netif" onchange="tabSelect(this.value);">
                                        </select>
                                    </div>

                                    <div class="tm-panel" style="margin-bottom:0;">
                                        <div id="net_chart" class="tm-chart"></div>
                                    </div>

                                    <table width="100%" align="center" cellpadding="4" cellspacing="0" class="table table-stat tm-panel">
                                        <tr>
                                            <th width="9%"></th>
                                            <th width="11%"><#Network#></th>
                                            <th width="20%" style="text-align: right"><#Current#></th>
                                            <th width="20%" style="text-align: right"><#Average#></th>
                                            <th width="20%" style="text-align: right"><#Maximum#></th>
                                            <th width="20%" style="text-align: right"><#Total#></th>
                                        </tr>
                                        <tr>
                                            <td width="9%" style="text-align:center; vertical-align: middle;">
                                                <div id="rx-sel" class="tm-swatch"></div>
                                            </td>
                                            <td width="11%"><#Downlink#></td>
                                            <td width="20%" valign="top"><span id="rx-current" class="tm-current"></span></td>
                                            <td width="20%" align="center" valign="top" style="text-align:right" id="rx-avg"></td>
                                            <td width="20%" align="center" valign="top" style="text-align:right" id="rx-max"></td>
                                            <td width="20%" align="center" valign="top" style="text-align:right" id="rx-total"></td>
                                        </tr>
                                        <tr>
                                            <td width="9%" style="text-align:center; vertical-align: middle;">
                                                <div id="tx-sel" class="tm-swatch tm-tx"></div>
                                            </td>
                                            <td width="11%"><#Uplink#></td>
                                            <td width="20%" valign="top"><span id="tx-current" class="tm-current"></span></td>
                                            <td width="20%" align="center" valign="top" style="text-align:right" id='tx-avg'></td>
                                            <td width="20%" align="center" valign="top" style="text-align:right" id='tx-max'></td>
                                            <td width="20%" align="center" valign="top" style="text-align:right" id='tx-total'></td>
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

    <div id="footer"></div>
</div>

</body>
</html>
