<?

//Instantiate the derivative class
$pdf=new REPORT();
$pdf->Open();
$pdf->AliasNbPages();


// First Page

$pdf->AddPage();

	$pdf->SetXY(70,80);
	$pdf->SetFont('Times','B',30);
	$pdf->Cell(70,0,$reportitle,0,1,'C');
	$pdf->Line(70,105,140,105);
	$pdf->Cell(0,70,$servertitle,0,1,'C');
	$pdf->RoundedRect(45, 50, 120, 105, 3.50, "1111", "D");
	$pdf->SetFont('Times','B',20);
	$pdf->SetXY(70,200);
	$pdf->Cell(70,5,$titreperiode1,0,1,'C');
	$pdf->SetFont('Times','B',16);
	$pdf->Cell(190,20,$titreperiode2,0,1,'C');
	$pdf->Cell(190,35,$versionrap,0,1,'C');
	$pdf->Ln(20);

//
// Insert the id card of servers here. If you don't want to use it, put N in 
// include/config.inc.php !
//
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
	$pdf->Image($imageload,10,26,0,13) ;
	$pdf->SetTitle($titreload);
	$pdf->Titre($titreload);
	if (file_exists($graphload)) {
		$pdf->Image($graphload,10,40,190,60) ;
	} else {
		$pdf->Cell(10,20,$nograph,0,1) ;
		$pdf->Ln(80);
	}

//
// Place the memory graph and text here
//
	$pdf->SetXY(25,115);
	$pdf->Image($imagemem,10,113,0,10) ;
	$pdf->SetTitle($titreram);
	$pdf->Titre($titreram);
	if (file_exists($graphram)) {
		$pdf->Image($graphram,10,125,190,60) ;
	} else {
		$pdf->Cell(10,20,$nograph,0,1) ;
		$pdf->Ln(80);
	}

//
// Place the network graph and text here
//
	$pdf->SetXY(25,200);
	$pdf->Image($imagenet,10,195,0,13) ;
	$pdf->SetTitle($titrenet);
	$pdf->Titre($titrenet);
	if (file_exists($graphnet)) {
		$pdf->Image($graphnet,10,210,190,65) ;
	} else {
		$pdf->Cell(10,20,$nograph,0,1) ;
		$pdf->Ln(20);
	}


// Third Page

$pdf->AddPage();

//
// Place the vmstat graph and text here
//
	$pdf->SetXY(25,30);
	$pdf->Image($imagecputil,10,26,0,13) ;
	$pdf->SetTitle($titrecputil);
	$pdf->Titre($titrecputil);
	if (file_exists($graphcputil)) {
		$pdf->Image($graphcputil,10,40,190,60) ;
	} else {
		$pdf->Cell(10,20,$nograph,0,1) ;
		$pdf->Ln(80);
	}

#####################################################
# Remove these graphes if you don't have ones !
#
if ( (file_exists($graphio)) && (file_exists($graphrq)) ) {

//
// Place the run-queue graph and text here
//
	$pdf->SetXY(25,115);
	$pdf->Image($imageio,10,113,0,10) ;
	$pdf->SetTitle($titreio);
	$pdf->Titre($titreio);
	$pdf->Image($graphio,10,125,190,60) ;

	$pdf->Ln(80);

//
// Place the I/O wait graph and text here
//
	$pdf->SetXY(25,200);
	$pdf->Image($imagerq,10,195,0,13) ;
	$pdf->SetTitle($titrerq);
	$pdf->Titre($titrerq);
	$pdf->Image($graphrq,10,210,190,65) ;

	$pdf->Ln(20);
}


$pdf->Output($output,'I');

?>
