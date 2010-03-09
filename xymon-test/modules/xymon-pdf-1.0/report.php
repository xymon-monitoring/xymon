<?

//
// Include config file
//
//////////////////////////
include "include/config.inc.php" ;


// Here we go ! We check the period time wanted and then generate the
// appropriate report.
switch($chkrap) 
{
case ($chkrap == "daily") :

	$graphload = $imgcpuload.$rangeday ;
	$graphram = $imgmem.$rangeday ;
	$graphnet = $imgnet.$rangeday ;
	$graphrq = $imgrq.$rangeday ;
	$graphio = $imgwio.$rangeday ;
	$graphcputil = $imgcputil.$rangeday ;

	include "report.skel.php" ;

break ;

case ($chkrap == "weekly") :

	$graphload=$imgcpuload.$rangeweek ;
	$graphram=$imgmem.$rangeweek ;
	$graphnet=$imgnet.$rangeweek ;
	$graphrq=$imgrq.$rangeweek ;
	$graphio=$imgwio.$rangeweek ;
	$graphcputil=$imgcputil.$rangeweek ;

	include "report.skel.php" ;

break ;

case ($chkrap == "biweek") :

	$graphload=$imgcpuload.$range2week ;
	$graphram=$imgmem.$range2week ;
	$graphnet=$imgnet.$range2week ;
	$graphrq=$imgrq.$range2week ;
	$graphio=$imgwio.$range2week ;
	$graphcputil=$imgcputil.$range2week ;

	include "report.skel.php" ;

break ;

case ($chkrap == "monthly") :

	$graphload = $imgcpuload.$rangemonth ;
	$graphram = $imgmem.$rangemonth ;
	$graphnet = $imgnet.$rangemonth ;
	$graphrq = $imgrq.$rangemonth ;
	$graphio = $imgwio.$rangemonth ;
	$graphcputil = $imgcputil.$rangemonth ;

	include "report.skel.php" ;

break ;

case ($chkrap == "annual") :

	$graphload = $imgcpuload.$rangeyear ;
	$graphram = $imgmem.$rangeyear ;
	$graphnet = $imgnet.$rangeyear ;
	$graphrq = $imgrq.$rangeyear ;
	$graphio = $imgwio.$rangeyear ;
	$graphcputil = $imgcputil.$rangeyear ;

	include "report.skel.php" ;

break ;

case ($chkrap == "analyse") :

	$graphload = $imgcpuload.$rangemonth ;
	$textload = $_POST['loadavg'] ;

	$graphram = $imgmem.$rangemonth ;
	$textram = $_POST['memory'] ;

	$graphnet = $imgnet.$rangemonth ;
	$textnet = $_POST['network'] ;

	$graphcputil = $imgcputil.$rangemonth ;
	$textcputil = $_POST['vmstat'] ;

	$graphrq = $imgrq.$rangemonth ;
	$textrq = $_POST['runqueue'] ;

	$graphio = $imgwio.$rangemonth ;
	$textio = $_POST['iowait'] ;

	$textconclusion = $_POST['conclusion'] ;

	$verif=$_POST['verif'] ;

	switch ($verif) 
	{
		case "ok" :
		$diagimg = $diaghealth ;
		$textdiag = $verifok ; 
		break ;
	
		case "nok" :
		$diagimg = $diagdanger ;
		$textdiag = $verifnok ;
		break ;
	
		case "mok" :
		$diagimg = $diagwarn ;
		$textdiag = $verifmok ;
		break ;
	}

	include "analysis.skel.php" ;

break ;

case ($chkrap == "glossaire") :

	include "glossaire.php" ;

break ;

} // End of 'switch - case'

?>
