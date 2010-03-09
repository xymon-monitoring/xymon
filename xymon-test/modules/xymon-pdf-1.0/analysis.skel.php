<?

//Instanciation de la classe derivee
$pdf=new ANALYSIS();
$pdf->Open();
$pdf->AliasNbPages();


// First Page

$pdf->AddPage();

	$pdf->SetXY(70,80);
	$pdf->SetFont('Times','B',30);
	$pdf->Cell(70,0,$title_analysis1,0,1,'C');
	$pdf->Line(70,105,140,105);
	$pdf->Cell(0,70,$title_analysis3,0,1,'C');
	$pdf->RoundedRect(45, 50, 120, 105, 3.50, "1111", "D");
	$pdf->SetFont('Times','B',20);
	$pdf->SetXY(70,200);
	$pdf->SetFont('Times','B',16);
	$pdf->Cell(70,5,$titreperiode,0,1,'C');
	$pdf->Cell(190,35,$versionrap,0,1,'C');
	$pdf->Ln(20);

// Insert the id card of servers here. If you don't want to use it, put N in rapport.php !
if ($idcard == "Y")
{
        include $idscript ;
}


// Second Page

$pdf->AddPage();

//
// Place the cpu load graph and text here
//
	$pdf->SetXY(25,30);
	$pdf->SetTitle($titreloadanalysis);
	$pdf->Titre($titreloadanalysis);
	$pdf->Image($imageload,10,26,0,13) ;
	if (file_exists($graphload)) {
		$pdf->Image($graphload,10,40,190,65);
		$pdf->Ln(70);
	} else {
		$pdf->Cell(10,20,$nograph,0,1) ;
		$pdf->Ln(40);
	}
	$pdf->SetFont('Times','',12);
	$pdf->MultiCell(0,5,$textload,0,'L') ;

//
// Place the memory graph and text here
//
	$pdf->SetXY(25,160);
	$pdf->SetTitle($titreramanalysis);
	$pdf->Titre($titreramanalysis) ;
	$pdf->Image($imagemem,10,158,0,10) ;
	if (file_exists($graphram)) {
		$pdf->Image($graphram,10,170,190,65);
		$pdf->Ln(70);
	} else {
		$pdf->Cell(10,20,$nograph,0,1) ;
		$pdf->Ln(40);
	}
	$pdf->SetFont('Times','',12);
	$pdf->MultiCell(0,5,$textram,0,'L') ;


// Third Page

$pdf->AddPage();

//
// Place the network graph and text here
//
	$pdf->SetXY(25,30);
	$pdf->SetTitle($titrenetanalysis);
	$pdf->Titre($titrenetanalysis);
	$pdf->Image($imagenet,10,26,0,13) ;
	if (file_exists($graphnet)) {
		$pdf->Image($graphnet,10,40,190,65);
		$pdf->Ln(70);
	} else {
		$pdf->Cell(10,20,$nograph,0,1) ;
		$pdf->Ln(40);
	}
	$pdf->SetFont('Times','',12);
	$pdf->MultiCell(0,5,$textnet,0,'L') ;

//
// Place the vmstat graph and text here
//
	$pdf->SetXY(25,160);
	$pdf->SetTitle($titrecputilanalysis);
	$pdf->Titre($titrecputilanalysis);
	$pdf->Image($imagecputil,12,155,0,14) ;
	if (file_exists($graphcputil)) {
		$pdf->Image($graphcputil,10,170,190,65);
		$pdf->Ln(70);
	} else {
		$pdf->Cell(10,20,$nograph,0,1) ;
		$pdf->Ln(40);
	}
	$pdf->SetFont('Times','',12);
	$pdf->MultiCell(0,5,$textcputil,0,'L') ;


// Fifth page

#####################################################
# remove the page if you don't want or don't have 
# SAR graphs/test.
#
if ( (file_exists($graphio)) && (file_exists($graphrq)) ) {

$pdf->AddPage();

//
// Place the run-queue graph and text here
//
	$pdf->SetXY(25,30);
	$pdf->SetTitle($titrerqanalysis);
	$pdf->Titre($titrerqanalysis);
	$pdf->Image($imagerq,11,28,0,11) ;
	$pdf->Image($graphrq,10,40,190,65);
	$pdf->Ln(70);
	$pdf->SetFont('Times','',12);
	$pdf->MultiCell(0,5,$textrq,0,'L') ;

//
// Place the I/O wait graph and text here
//
	$pdf->SetXY(25,160);
	$pdf->SetTitle($titreioanalysis);
	$pdf->Titre($titreioanalysis);
	$pdf->Image($imageio,10,155,0,13) ;
	$pdf->Image($graphio,10,170,190,65);
	$pdf->Ln(70);
	$pdf->SetFont('Times','',12);
	$pdf->MultiCell(0,5,$textio,0,'L') ;

}


// Sixth and last page

$pdf->AddPage();

//
// Place the conclusion text here
//
	$pdf->SetXY(25,30);
	$pdf->SetTitle($titreconclusion);
	$pdf->Titre($titreconclusion);
	$pdf->Image($imageconclusion,12,28,0,10) ;
	$pdf->SetFont('Times','',12);
	$pdf->MultiCell(0,5,$textconclusion,0,'L') ;

//
// Place the diagnostic graph and text here
//
	$pdf->SetXY(10,120);
	$pdf->SetTitle($titrediag);
	$pdf->Titre($titrediag) ;
	$pdf->Image($diagimg,78,140,0,0);
	$pdf->Ln(70);
	$pdf->SetFont('Times','B',18);
	$pdf->Cell(0,5,$textdiag,0,1,'C') ;

// Display PDF in the browser directly
$pdf->Output($outputanalysis,'I');

?>
