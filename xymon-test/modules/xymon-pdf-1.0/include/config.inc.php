<?

///////////////////////////////////////////////////////////////////////////////
//
// Here is the main configuration script. All you may modify is defined here.
//
///////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////
//
// Path to FPDF
//
////////////////////////////////////////////
$pathfpdf = '/usr/lib/tcpdf' ;

//////////////////////////
//
// Include FPDF
//
//////////////////////////
define('FPDF_FONTPATH',"$pathfpdf/fonts/") ;
require "$pathfpdf/tcpdf.php" ;

////////////////////////////////////////////
//
// Setting the locales according to your country
//
////////////////////////////////////////////
setlocale(LC_ALL, "fr_FR") ;

////////////////////////////////////////////
//
// Full paths to scripts and images
//
////////////////////////////////////////////
$pdfhome = '/data/hobbit/server/www/pdf' ;

// if you modify these, don't forget to do 
// the same in the shell scripts !
$myimg = $pdfhome.'/images' ;
$str1 = $pdfhome.'/graphrrd/' ;

////////////////////////////////////////////
//
// Version appears on the first page
//
////////////////////////////////////////////
$versionrap = 'Version 1.0' ;
        
////////////////////////////////////////////
//
// Classes declaration
//
////////////////////////////////////////////
require "$pdfhome/include/class.report.inc.php" ;
require "$pdfhome/include/class.analysis.inc.php" ;
//require "$pdfhome/include/class.glossary.inc.php" ;

////////////////////////////////////////////
//
// Type for the report : daily, monthly, etc...
// You can select the type from pdf.php
//
////////////////////////////////////////////
$chkrap=$_POST['chxrap'] ;

////////////////////////////////////////////
//
// Name of the server provided by pdf.php
//
////////////////////////////////////////////
$nmsrv=$_POST['nom'] ;
$srvname = strtolower($nmsrv) ; 

////////////////////////////////////////////
//
// Settings variables for date manipulations
//
////////////////////////////////////////////
//
// Are we before or after the 16 of current month ?
//
$m=strftime("%m");
$y=strftime("%G");

if (strftime("%d") < 16)
{
        $mois = mktime( 0, 0, 0, ($m-1), 1, $y );
        $mois_rap = strftime("%B %G",$mois);
        $mois_num = strftime("%m",$mois);
}
else
{
        $mois_rap = strftime("%B %G");
        $mois_num = strftime("%m");
}
//
////////////////////////////////////////////

////////////////////////////////////////////
//
// Do you want to add a page wich describe
// the server before graphs - 'id card' (y/N) ?
// I'm getting info about servers from a 
// MySQL DB here at my work. So you probably
// want to say No unless you have the same :) 
//
////////////////////////////////////////////
$idcard = 'N' ;

////////////////////////////////////////////
//
// If you answer 'Y' to idcard, please provide
// the name and / or path of the script used.
//
////////////////////////////////////////////
$idscript = 'id.card.php' ;

////////////////////////////////////////////
//
// If you answer 'Y' to idcard, please provide
// here the MySQL configuration.
//
////////////////////////////////////////////
$host = '';
$user = '';
$password = '';
$db = '';
$msgerror = 'Can\'t connect to server.' ;

////////////////////////////////////////////
//
// Define here your request to get infos
// from your MySQL DB.
//
////////////////////////////////////////////
$sql_idcard = "" ;

////////////////////////////////////////////
//
// Define here your other MySQL requests 
//
////////////////////////////////////////////
$sql_dsi = "" ;

$sql_tp = "" ;

$sql_bt = "" ;

////////////////////////////////////////////
//
// Sentences commonly used in the documents
//
////////////////////////////////////////////
//
// Sentences embedded in ID card
//
$titreidcard = 'ID Card' ;
$idcardhost = 'Hostname :' ;
$idcardalias = 'Alias :' ;
$idcardgroup = 'Group :' ;
$idcardproj = 'Project :' ;
$idcardloc = 'Localisation :' ;
$idcardnbcpu = 'CPU Nb :' ;
$idcardfamily = 'CPU family :' ;
$idcardfreq = 'CPU Clock Speed :' ;
$idcardram = 'RAM :' ;
$idcardswap = 'Swap :' ;
$idcardos = 'OS :' ;
$idcardfonc = 'Function :' ;
$idcardda = 'Date of billing :' ;
$idcardconst = 'Constructor :' ;
$idcardmodel = 'Model :' ;
$idcardvirtual = 'Virtual Machine :' ;
$idcardsan = 'Linked to SAN :' ;
$idcardno = 'No' ;
$idcardyes = 'Yes' ;
$idcardnbhba = 'HBA FC Nb :' ;
$idcardmodelhba = 'HBA FC model :' ;

//
// Sentences embedded in Reports or analysis
//
$reportitle = 'Monitoring report' ;
$servertitle = $srvname ;

switch($chkrap)
{

	case "daily" :
	$period = 'Daily' ;
	$titreperiode2 = strftime("%d-%b-%Y") ;
	$output = $srvname.'.'.$titreperiode2.'.pdf' ;
	break ;

	case "weekly" :
	$period = 'Weekly' ;
	// Past week
        $semaine1 = strftime("%V") - 1 ;
        $titreperiode2 = 'Week '.$semaine1 ;
	$output = $srvname.'.'.$titreperiode2.'.pdf' ;
	break ;

	case "biweek" :
	$period = 'Bi-weekly' ;
	// Past and present week
        $semaine1 = strftime("%V") - 2 ;
        $semaine2 = strftime("%V") - 1 ;
	$titreperiode2='Weeks '.$semaine1.'-'.$semaine2 ;
	$output = $srvname.'.'.$titreperiode2.'.pdf' ;
	break ;

	case "monthly" :
	$period = 'Monthly' ;
	$titreperiode2 = $mois_rap ;
	$output = $srvname.'.'.$titreperiode2.'.pdf' ;
	break ;

	case "annual" :
	$period = 'Annual' ;
	$titreperiode2 = strftime("%G") ;
	$output = $srvname.'.'.$titreperiode2.'.pdf' ;
	break ;

	case "dsimensuel" :
	$period = 'Monthly' ;
	$output = $srvname.'.'.$titreperiode2.'.pdf' ;
	break ;

	default :
	$period = 'none' ;
	break ;

} // End of switch - case

//
// Now we store 'real' sentences according to the $period variable.
// That's for REPORTS ONLY
//
$titreperiode1 = $period.' report' ;
$titreload = $period.' graph for CPU load' ;
$titreram = $period.' graph for memory utilization' ;
$titrenet = $period.' graph for network traffic' ;
$titrerq = $period.' graph for run-queue length' ;
$titreio = $period.' graph for I/O wait rate' ;
$titrecputil = $period.' graph for CPU utilization' ;

//
// These words or sentences are for ANALYSIS ONLY
//
$title_analysis1 = 'Analysis' ;
$title_analysis2 = 'of' ;
$title_analysis3 = $srvname ;
$titreperiode = $mois_rap ;
$titreloadanalysis = 'CPU Load' ;
$titreramanalysis = 'Memory Utilization' ;
$titrenetanalysis = 'Network Traffic' ;
$titrerqanalysis = 'Run-queue Length';
$titreioanalysis = 'I/O wait Rate' ;
$titrecputilanalysis = 'CPU Utilization' ;
$titreconclusion = 'Conclusion' ;
$titrediag = 'Diagnostic' ;
$verifok = $srvname.' is healthy' ;
$verifnok = $srvname.' is heavy-loaded' ;
$verifmok = $srvname.' is under-loaded' ;
// Name of the PDF document
$outputanalysis = $srvname.'.Analysis.'.strftime("%B %G").'.pdf' ;
//
////////////////////////////////////////////


////////////////////////////////////////////
//
// Paths for graphs used in the documents
//
////////////////////////////////////////////
//
// Paths for rrd graphs.
//
// Don't forget that these paths and names 
// are defined by shell scripts found into
// the 'scripts' directory.
//
// When no graph is found, display this text
// instead.
$nograph = 'No graph' ;
$extn = 'png' ;
$str2 = '/load.' ;
$str3 = '/mem.' ;
$str4 = '/rq.' ;
$str5 = '/netio.' ;
$str6 = '/cputil.' ;
$str7 = '/wio.' ;
$rangeday = '-day.'.$extn ;
$rangeweek = '-week.'.$extn ;
$range2week = '-2week.'.$extn ;
$rangemonth = '-month.'.$extn ;
$rangeyear = '-year.'.$extn ;
$imgcpuload = $str1.$srvname.$str2.$srvname ;
$imgmem = $str1.$srvname.$str3.$srvname ;
$imgrq = $str1.$srvname.$str4.$srvname ;
$imgnet = $str1.$srvname.$str5.$srvname ;
$imgcputil = $str1.$srvname.$str6.$srvname ;
$imgwio = $str1.$srvname.$str7.$srvname ;
//
////////////////////////////////////////////


////////////////////////////////////////////
//
// Paths for images used in the documents
//
////////////////////////////////////////////
//
// Image for idcard
//
$idcardimg = $myimg.'/idcard.png' ;

//
// Images used by reports and analysis
//
$imageload = $myimg.'/procman.png' ;
$imagemem = $myimg.'/ramstick.png' ;
$imagenet = $myimg.'/gnome-fs-network.png' ;
$imagerq = $myimg.'/queue.png' ;
$imageio = $myimg.'/hard_drive.png' ;
$imagecputil = $myimg.'/cpu_P4_2.png' ;
$imageconclusion = $myimg.'/bar_chart.png' ;
$diaghealth = $myimg.'/checkmark.png' ;
$diagwarn = $myimg.'/warning.png' ;
$diagdanger = $myimg.'/danger.png' ;
//
////////////////////////////////////////////

?>
