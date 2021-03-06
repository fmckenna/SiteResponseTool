#include <QQuickView>
#include "RockOutcrop.h"
#include "ui_RockOutcrop.h"
#include "InsertWindow.h"
#include <QQmlContext>

#include <QTime>
#include <QTimer>

#include <QtQuick/qquickitem.h>
#include <QtQuick/qquickview.h>

#include <QStringList>

#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QThread>

#include <QTabWidget>

#include "SiteResponse.h"

#include <QUiLoader>

#include <QFileInfo>
#include <QMessageBox>
#include <QSizePolicy>
#include <QTabBar>


#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <iomanip>



RockOutcrop::RockOutcrop(QWidget *parent) :
    SimCenterAppWidget(parent),
    ui(new Ui::RockOutcrop)
{

    // create analysis dir
    if(!QDir(analysisDir).exists())
        QDir().mkdir(analysisDir);

    // random seed
    unsigned uintTime = static_cast<unsigned>(QTime::currentTime().msec());
    qsrand(uintTime);

    ui->setupUi(this);
    updateCtrl();

    // page setting
    QStringList pages;
    int totalPage = ui->tableView->totalPage();
    for( int i = 1; i <= totalPage; ++i)
        pages.append( QString("%1").arg(i) );
    ui->gotoPageBox->addItems(pages);

    // some connections
    connect(ui->tableView, SIGNAL(insertAct()), this, SLOT(insertAbove()) );
    connect(ui->tableView, SIGNAL(insertBelowAct()), this, SLOT(insertBelow()) );
    connect(ui->tableView, SIGNAL(removeAct()), this, SLOT(remove()) );
    connect(ui->prePageBtn, SIGNAL(clicked()), this, SLOT(prevPage()) );
    connect(ui->nextPageBtn, SIGNAL(clicked()), this, SLOT(nextPage()) );
    connect(ui->gotoPageBox, SIGNAL(currentIndexChanged(int)), this, SLOT(gotoPage(int)) );
    connect(ui->totalLayerLineEdit, SIGNAL(editingFinished()), this, SLOT(onTotalLayerEdited()) );

    // styling some buttons
    ui->prePageBtn->setFocusPolicy(Qt::NoFocus);
    ui->prePageBtn->setFixedSize(20, 20);
    ui->prePageBtn->setIconSize(ui->prePageBtn->size());
    ui->prePageBtn->setStyleSheet("border:none;");
    ui->prePageBtn->setIcon(QIcon(":/resources/images/left.png"));

    ui->nextPageBtn->setFocusPolicy(Qt::NoFocus);
    ui->nextPageBtn->setFixedSize(20, 20);
    ui->nextPageBtn->setIconSize(ui->nextPageBtn->size());
    ui->nextPageBtn->setStyleSheet("border:none;");
    ui->nextPageBtn->setIcon(QIcon(":/resources/images/right.png"));

    ui->gotoPageBox->setFixedWidth(60);
    ui->gotoPageBox->hide();
    ui->curPageLabel->hide();


    // set the global stylesheet
    QFile file(":/resources/styles/stylesheet.css");
    if(file.open(QFile::ReadOnly)) {
      QString styleSheet = QLatin1String(file.readAll());
      this->setStyleSheet(styleSheet);
    }


    // validator for total layers, must be int between 0 and MAXLAYERS
    ui->totalLayerLineEdit->setValidator(new QIntValidator(0, MAXLAYERS, this));

    // when an element is selected, the whole row is selected
    ui->tableView->setSelectionBehavior(QAbstractItemView::SelectRows);

    // validator for height, must be double
    ui->totalHeight->setValidator(new QDoubleValidator(0.0,10000.0,2,ui->totalHeight));

    // change the total height if soil layer is edited
    connect(ui->totalHeight, SIGNAL(editingFinished()),this, SLOT(totalHeightChanged()) );
    connect(ui->gwtEdit, SIGNAL(editingFinished()), this, SLOT(on_gwtEdit_editingFinished()));

    // set limits for ground water table to be (0.0,1000)
    ui->gwtEdit->setValidator(new QDoubleValidator(0.0,10000.0,2,ui->gwtEdit));

    // height and count of layers not allowed to edit
    ui->totalHeight->setReadOnly(true);
    ui->totalLayerLineEdit->setReadOnly(true);







    //--------------------------------------------------------------------------------//
    // Init Mesher and Mesh View
    //--------------------------------------------------------------------------------//

    // init a 2D mesher
    mesher = new Mesher(srtFileName.toStdString());
    mesher->mesh2DColumn();


    // init the elements model to be used in mesh plot
    elementModel = new ElementModel;
    //std::sort(mesher->elements.begin(),mesher->elements.end(),
    //          [](const Quad &a, const Quad &b) { return  a.tag() > b.tag(); });
    elementModel->clear();
    elementModel->setWidth(mesher->eSizeH());
    elementModel->setTotalHeight(ui->totalHeight->text().toDouble());
    elementModel->setNodes(mesher->nodes);
    for(std::vector<int>::size_type n = mesher->elements.size(); n > 0; n--)
    {
        int tag = mesher->elements[n-1]->tag();
        int i = mesher->elements[n-1]->i();
        int j = mesher->elements[n-1]->j();
        int k = mesher->elements[n-1]->k();
        int l = mesher->elements[n-1]->l();
        double t = mesher->elements[n-1]->thickness();
        QString color = QString::fromStdString(mesher->elements[n-1]->color());
        bool isActive = false;
        elementModel->addElement("quad",tag,i,j,k,l,t,color,isActive);
    }
    elementModel->refresh();


    // add QQuickwidget for displaying mesh
    meshView = new QQuickView();
    meshView->setSource(QUrl(QStringLiteral("qrc:/resources/ui/MeshView.qml")));
    meshView->rootContext()->setContextProperty("elements", elementModel);
    //meshView->rootContext()->setContextProperty("GWT", ui->gwtEdit->text().toDouble());
    //meshView->rootContext()->setContextProperty("totalHeightEdt", ui->totalHeight);
    meshView->rootContext()->setContextProperty("sqlModel", ui->tableView->m_sqlModel);
    QWidget *meshContainer = QWidget::createWindowContainer(meshView, this);






    resultsTab = new QTabWidget;
    resultsTab->setMinimumSize(200,layerTableHeight);
    resultsTab->setMaximumSize(200,1e5);
    resultsTab->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    resultsTab->addTab(meshContainer,"Mesh");

    postProcessor = new PostProcessor(outputDir);
    profiler = new ProfileManager(resultsTab, postProcessor ,this);   
    //connect(postProcessor, SIGNAL(updateFinished()), profiler, SLOT(onPostProcessorUpdated()));
    //postProcessor->update();


    // add the profile tab into the ui's layout
    ui->meshLayout->addWidget(resultsTab);

    // set stylesheet for the profile tab
    QFile profileCSSfile(":/resources/styles/profile.css");
    if(profileCSSfile.open(QFile::ReadOnly)) {
      QString styleSheet = QLatin1String(profileCSSfile.readAll());
      //QTabBar *configTabBar = resultsTab->findChild<QTabBar *>("qt_tabwidget_tabbar");
      resultsTab->setStyleSheet(styleSheet);
      profileCSSfile.close();
    }










    // add QQuickwidget for displaying soil layers
    QQuickView *plotView = new QQuickView();
    plotView->rootContext()->setContextProperty("designTableModel", ui->tableView);
    plotView->rootContext()->setContextProperty("soilModel", ui->tableView->m_sqlModel);
    plotContainer = QWidget::createWindowContainer(plotView, this);
    //plotContainer->setFixedSize(QSize(200, 800));
    //plotContainer->setMinimumSize(layerViewWidth,layerTableHeight);
    plotContainer->setMinimumSize(200,layerTableHeight);
    plotContainer->setMaximumSize(200,1e5);
    plotContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    //plotContainer->setFocusPolicy(Qt::TabFocus);
    plotView->setSource(QUrl(QStringLiteral("qrc:/resources/ui/PlotView.qml")));
    ui->soilColumnLayout->addWidget(plotContainer);
    //plotContainer->setWindowFlags(Qt::Widget | Qt::FramelessWindowHint | Qt::WindowSystemMenuHint | Qt::WindowStaysOnTopHint);



    // hide some buttons
    ui->nextPageBtn->hide();
    ui->prePageBtn->hide();
    ui->reBtn->setVisible(false);
    ui->meshBtn->setVisible(true);

    // add some connections
    connect(ui->meshBtn, SIGNAL(clicked()), this, SLOT(on_meshBtn_clicked(bool)) );
    connect(ui->tableView->m_sqlModel, SIGNAL(thicknessEdited()), this, SLOT(on_thickness_edited()));
    connect(ui->tableView, SIGNAL(rowRemoved(int)), this, SLOT(on_rowRemoved(int)));
    //connect(ui->tableView, SIGNAL(pressed(const QModelIndex &)), ui->tableView, SLOT(on_activated(const QModelIndex &)));
    connect(this, SIGNAL(tableMoved()), this, SLOT(refresh()));

    resize(830 + 80, 350 + 40);
    //resize(830 + 80 + 280, 530 + 20);

    ui->tableView->m_sqlModel->deActivateAll();




    // init the tab manager on the right-bottom
    theTabManager = new TabManager(ui->tableView, elementModel ,this);
    theTabManager->updatePostProcessor(postProcessor);
    theTabManager->init(ui->tabWidget);
    //connect(ui->tableView, SIGNAL(cellClicked(const QModelIndex &)), theTabManager, SLOT(onTableViewClicked(const QModelIndex &)));
    connect(ui->tableView->m_sqlModel, SIGNAL(dataChanged(const QModelIndex&,const QModelIndex&)), theTabManager, SLOT(onTableViewUpdated(const QModelIndex&,const QModelIndex&)));
    connect(ui->gwtEdit, SIGNAL(editingFinished()), theTabManager, SLOT(onFEMTabEdited()));
    connect(this, SIGNAL(runBtnClicked(QWebEngineView*)), theTabManager, SLOT(onRunBtnClicked(QWebEngineView*)));
    // if configure tab changed, update SRT.json
    connect(theTabManager, SIGNAL(configTabUpdated()), this, SLOT(onConfigTabUpdated()));

    // init the dino view
    dinoView = new QWebEngineView(this);

    dinoView->load(QUrl::fromLocalFile(dinoHtmlName));
    dinoView->setVisible(false);
    //dinoView->setMinimumSize(layerTableWidth,300);
    //dinoView->setMaximumSize(1e5,1e5);
    //dinoView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // init GWT
    ui->gwtEdit->setText(QString::number(theTabManager->getGWTFromConfig()));

    // size control on the right
    ui->tableView->setMinimumSize(layerTableWidth,200);
    ui->tableView->setMaximumSize(1e5,1e5);
    ui->tableView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->tabWidget->setMinimumSize(layerTableWidth,300);
    ui->tabWidget->setMaximumSize(1e5,1e5);
    ui->tabWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);


    if (useJSONasInput)
    {
        elementModel->clear();
        elementModel->refresh();
        //loadFromJson();
    }else{
        // add a default layer
        if(ui->tableView->m_sqlModel->rowCount()<1)
        {
            QList<QVariant> valueListRock;
            valueListRock << "Rock" << "-" << DefaultDensity << DefaultVs << DefaultEType << "-";
            ui->tableView->insertAt(valueListRock,0);
            /*
        QList<QVariant> valueList;
        valueList << "Layer 1" << DefaultThickness << DefaultDensity << DefaultVs << DefaultEType << DefaultESize;
        ui->tableView->insertAt(valueList,0);
        */
            ui->tableView->setTotalHeight(0);
            ui->totalHeight->setText("0");
            ui->totalLayerLineEdit->setText("1");
        }
    }


    // init the opensess process
    openseesProcess = new QProcess(this);
    openseesProcess->setWorkingDirectory(analysisDir);
    //connect(openseesProcess, SIGNAL(readyReadStandardOutput()),this,SLOT(onOpenSeesFinished()));
    connect(openseesProcess, SIGNAL(readyReadStandardError()),this,SLOT(onOpenSeesFinished()));

    if(!QDir(outputDir).exists())
        QDir().mkdir(outputDir);


}

void RockOutcrop::on_gwtEdit_editingFinished()
{
    // update SRT.json
    ui->reBtn->click();
}

void RockOutcrop::onConfigTabUpdated()
{
    // update SRT.json
    ui->reBtn->click();
}


void RockOutcrop::loadFromJson()
{


    QString in;
    QFile inputFile(srtFileName);
    if(inputFile.open(QFile::ReadOnly)) {
    inputFile.open(QIODevice::ReadOnly | QIODevice::Text);
    in = inputFile.readAll();
    inputFile.close();
    }else{
        // if not input file provided add a default layer
        if(ui->tableView->m_sqlModel->rowCount()<1)
        {
            QList<QVariant> valueListRock;
            valueListRock << "Rock" << "-" << DefaultDensity << DefaultVs << DefaultEType << "-";
            ui->tableView->insertAt(valueListRock,0);
            ui->tableView->setTotalHeight(0);
            ui->totalHeight->setText("0");
            ui->totalLayerLineEdit->setText("1");
        }
    }

    QJsonDocument indoc = QJsonDocument::fromJson(in.toUtf8());
    //qWarning() << indoc.isNull();
    QJsonObject inobj = indoc.object();
    qWarning() << inobj.value(QString("author"));
    qWarning() << inobj["author"];
    qWarning() << inobj["soilProfile"].toObject()["soilLayers"].toArray();

    QJsonArray soilLayers = inobj["soilProfile"].toObject()["soilLayers"].toArray();
    QJsonArray materials = inobj["materials"].toArray();
    for (int i=soilLayers.size()-1; i>=0; i--)
    {
        QJsonObject l = soilLayers[i].toObject();
        QString name = l["name"].toString();
        QString color = l["color"].toString();
        int id = l["id"].toInt();
        QJsonObject mat = materials[i].toObject();
        QString material = materials[i].toObject()["type"].toString();
        double Dr = l["Dr"].toDouble();
        double density = l["density"].toDouble();
        double eSize = l["eSize"].toDouble();
        double hPerm = l["hPerm"].toDouble();
        double vPerm = l["vPerm"].toDouble();
        double thickness = l["thickness"].toDouble();
        double vs = l["vs"].toDouble();

        if (i==soilLayers.size()-1)// Rock
        {
            QList<QVariant> valueListRock;
            valueListRock << "Rock" << "-" << density << vs << DefaultEType << "-";
            ui->tableView->insertAt(valueListRock,0);
        }else{
            QList<QVariant> valueList;
            if (color=="")
                color = QColor::fromRgb(QRandomGenerator::global()->generate()).name();
            valueList << name << thickness << density << vs << material << eSize << color;
            ui->tableView->insertAtSilent(valueList,0);

        }

        theTabManager->updateLayerTab(l,mat);

    }



    QJsonObject basicSettings = inobj["basicSettings"].toObject();
    QString groundMotion = basicSettings["groundMotion"].toString();
    theTabManager->updateGMPath(groundMotion);
    QString OpenSeesPath = basicSettings["OpenSeesPath"].toString();
    theTabManager->updateOpenSeesPath(OpenSeesPath);

    ui->gwtEdit->setText(QString::number(basicSettings["groundWaterTable"].toDouble()));

    ui->totalLayerLineEdit->setText(QString::number(soilLayers.size()));


    ui->reBtn->click();

}


bool
RockOutcrop::outputAppDataToJSON(QJsonObject &jsonObject) {

    //
    // per API, need to add name of application to be called in AppLication
    // and all data to be used in ApplicationDate
    //

    jsonObject["EventClassification"]="Earthquake";
    jsonObject["Application"] = "Site Response";
    QJsonObject dataObj;
    jsonObject["ApplicationData"] = dataObj;
    return true;
}

bool
RockOutcrop::inputAppDataFromJSON(QJsonObject &jsonObject) {
    return true;
}

bool RockOutcrop::inputFromJSON(QJsonObject& inobj) {

    qWarning() << inobj.value(QString("author"));
    qWarning() << inobj["author"];
    qWarning() << inobj["soilProfile"].toObject()["soilLayers"].toArray();

    QJsonArray soilLayers = inobj["soilProfile"].toObject()["soilLayers"].toArray();
    QJsonArray materials = inobj["materials"].toArray();
    for (int i=soilLayers.size()-1; i>=0; i--)
    {
        QJsonObject l = soilLayers[i].toObject();
        QString name = l["name"].toString();
        QString color = l["color"].toString();
        int id = l["id"].toInt();
        QJsonObject mat = materials[i].toObject();
        QString material = materials[i].toObject()["type"].toString();
        double Dr = l["Dr"].toDouble();
        double density = l["density"].toDouble();
        double eSize = l["eSize"].toDouble();
        double hPerm = l["hPerm"].toDouble();
        double vPerm = l["vPerm"].toDouble();
        double thickness = l["thickness"].toDouble();
        double vs = l["vs"].toDouble();

        if (i==soilLayers.size()-1)// Rock
        {
            QList<QVariant> valueListRock;
            valueListRock << "Rock" << "-" << density << vs << DefaultEType << "-";
            ui->tableView->insertAt(valueListRock,0);
        }else{
            QList<QVariant> valueList;
            if (color=="")
                color = QColor::fromRgb(QRandomGenerator::global()->generate()).name();
            valueList << name << thickness << density << vs << material << eSize << color;
            ui->tableView->insertAtSilent(valueList,0);

        }

        theTabManager->updateLayerTab(l,mat);

    }



    QJsonObject basicSettings = inobj["basicSettings"].toObject();
    QString groundMotion = basicSettings["groundMotion"].toString();
    theTabManager->updateGMPath(groundMotion);
    QString OpenSeesPath = basicSettings["OpenSeesPath"].toString();
    theTabManager->updateOpenSeesPath(OpenSeesPath);

    ui->gwtEdit->setText(QString::number(basicSettings["groundWaterTable"].toDouble()));

    ui->totalLayerLineEdit->setText(QString::number(soilLayers.size()));


    ui->reBtn->click();

    return true;
}

RockOutcrop::~RockOutcrop()
{
    delete ui;
}

ElementModel* RockOutcrop::getElementModel() const
{
    return elementModel;
}

void RockOutcrop::onTotalLayerEdited()
{
    double previousHeight = ui->totalHeight->text().toDouble();
    int previousNumLayers = ui->tableView->totalSize();
    int newNumLayers = ui->totalLayerLineEdit->text().toInt();

    if(newNumLayers>MAXLAYERS | abs(newNumLayers)<1e-5)
    {
        ui->totalLayerLineEdit->setText(QString::number(previousNumLayers));
        QMessageBox::information(nullptr, "error", "Num of layers could not be zero or greater than "+QString::number(MAXLAYERS)+".");
    }
    else{

    if(previousNumLayers < newNumLayers)
    {
        QList<QVariant> emptyList;
        for (int i=0; i<(newNumLayers-previousNumLayers); i++)
            ui->tableView->insertAtEnd(emptyList);
    }
    if(previousNumLayers > newNumLayers & newNumLayers>=0)
    {
        for (int i=previousNumLayers; i>newNumLayers; i--)
            ui->tableView->removeOneRow(i-1);
    }
    if(previousNumLayers != newNumLayers)
    {
        qDebug() << "total layer changed from "<< previousNumLayers << " to " << newNumLayers;
        ui->tableView->divideByLayers(previousHeight,newNumLayers);
    }

    }

}



void RockOutcrop::on_meshBtn_clicked(bool checked)
{

    Q_UNUSED(checked);
    if (resultsTab->isVisible()){
        ui->meshBtn->setText(">");

        /*
        QPropertyAnimation *animation= new QPropertyAnimation(this, "windowOpacity");
        animation->setDuration(1000);
        animation->setStartValue(1);
        animation->setEndValue(0);
        animation->start();
        */

        QPropertyAnimation *anim1=new QPropertyAnimation(resultsTab, "pos");

        anim1->setDuration(300);

        anim1->setStartValue(resultsTab->pos());

        anim1->setEndValue(QPoint(resultsTab->pos().x()-220,resultsTab->pos().y()));

        anim1->setEasingCurve(QEasingCurve::OutBounce);

        anim1->start();


        QTimer::singleShot(300, this, SLOT(hideShowTab()));


        ui->coolSpacer->changeSize(0,0,QSizePolicy::Maximum,QSizePolicy::Maximum);
    }
    else{

        QPropertyAnimation *anim1=new QPropertyAnimation(resultsTab, "pos");

        anim1->setDuration(100);

        anim1->setStartValue(resultsTab->pos());

        anim1->setEndValue(QPoint(resultsTab->pos().x()+220,resultsTab->pos().y()));

        anim1->setEasingCurve(QEasingCurve::OutBounce);

        anim1->start();

        ui->meshBtn->setText("<");
        resultsTab->setVisible(true);
        ui->coolSpacer->changeSize(5,5,QSizePolicy::Maximum,QSizePolicy::Maximum);
        plotContainer->setVisible(false);
        plotContainer->setVisible(true);
    }
    /*
    if (ui->groupBox_Mesh->isVisible())
    {
        ui->groupBox_Mesh->setVisible(false);

        int w = layerViewWidth + ui->groupBox_SoilLayersTable->size().width();
        int h = ui->groupBox_Graphic->size().height() ;
        this->resize(w+80,h+20);
    }else{
        ui->groupBox_Mesh->setVisible(true);
        //ui->groupBox_Graphic->setVisible(false);
        //int w =  meshViewWidth + ui->groupBox_SoilLayersTable->size().width();

        int w = layerViewWidth + meshViewWidth + ui->groupBox_SoilLayersTable->size().width();
        int h = ui->groupBox_Graphic->size().height() ;
        this->resize(w+80,h+20);
    }
    */

}

void RockOutcrop::hideShowTab()
{
    resultsTab->setVisible(false);
}

void RockOutcrop::updateCtrl()
{

    ui->totalHeight->setText(QString::number(ui->tableView->totalHeight()));
    ui->gwtEdit->setText(QString::number(ui->tableView->getGWT()));

    int total = ui->tableView->totalSize();
    if(total>1)
    {
        //ui->totalLabel->setText( tr("Total Layers %1").arg(total) );
        ui->totalLabel->setText( tr("Total Layers") );
        ui->totalLayerLineEdit->setText(QString::number(total));
    }else{
        //ui->totalLabel->setText( tr("Total Layer %1").arg(total) );
        ui->totalLabel->setText( tr("Total Layer"));
        ui->totalLayerLineEdit->setText(QString::number(total));
    }

    int curPage = ui->tableView->currentPage();
    int totalPage = ui->tableView->totalPage();
    ui->curPageLabel->setText( tr("Page %1/%2").arg(curPage).arg(totalPage));

    ui->nextPageBtn->setEnabled(true);
    ui->prePageBtn->setEnabled(true);

    if ( totalPage == 1)
    {
        ui->nextPageBtn->setDisabled(true);
        ui->prePageBtn->setDisabled(true);
    }else{
        if(curPage >= totalPage)
        {
            ui->nextPageBtn->setDisabled(true);
        }else if(curPage == 1)
        {
            ui->prePageBtn->setDisabled(true);
        }else
        {

        }
    }

}

void RockOutcrop::insertWithDialog()
{

}



void RockOutcrop::insert()// insert from dialog
{

    QList<QVariant> emptyList;
    ui->tableView->insertBelow(emptyList);
    /*
    InsertWindow* insertDlg = new InsertWindow(this);
    QList<QVariant> infos = ui->tableView->currentRowInfo();
    if( infos.count() >= 5)
        insertDlg->initInfo(infos);

    connect(insertDlg, SIGNAL(accepted(QList<QVariant>)),
            ui->tableView, SLOT(insert(QList<QVariant>)) );
    int ok = insertDlg->exec();
    if( ok )
    {
        updateCtrl();
    }
    insertDlg->deleteLater();

    int totalPage = ui->tableView->totalPage();
    int count = ui->gotoPageBox->count();
    if( count < totalPage)
    {
        ui->gotoPageBox->addItem(QString("%1").arg(count+1));
    }
    */

}

void RockOutcrop::insertAbove()
{
    QList<QVariant> emptyList;
    ui->tableView->insertAbove(emptyList);
    updateCtrl();
    ui->reBtn->click();
}

void RockOutcrop::insertBelow()
{
    QList<QVariant> emptyList;
    ui->tableView->insertBelow(emptyList);
    updateCtrl();
    ui->reBtn->click();

}

void RockOutcrop::remove()
{
    ui->tableView->remove();
    updateCtrl();
    int totalPage = ui->tableView->totalPage();

    int count = ui->gotoPageBox->count();
    if( count > totalPage)
    {
        ui->gotoPageBox->removeItem(count-1);
    }
    ui->reBtn->click();
}

void RockOutcrop::gotoPage(int index)
{
    index++;
    ui->tableView->gotoPage(index);
    updateCtrl();
}

void RockOutcrop::nextPage()
{
    ui->tableView->nextPage();
    updateCtrl();
}

void RockOutcrop::prevPage()
{
    ui->tableView->previousPage();
    updateCtrl();
}

void RockOutcrop::on_delRowBtn_clicked()
{
    emit ui->tableView->removeAct();
}

void RockOutcrop::on_addRowBtn_clicked()
{
    emit ui->tableView->insertBelowAct();
}

/**
 * when user input a new height value
 * @brief RockOutcrop::totalHeightChanged
 * @return
 */
void RockOutcrop::totalHeightChanged()
{
    if(abs(ui->totalHeight->text().toDouble() - ui->tableView->totalHeight())>1e-5)
    {
        ui->tableView->setTotalHeight(ui->totalHeight->text().toDouble());
        ui->tableView->divideByLayers(ui->totalHeight->text().toDouble(), ui->tableView->totalSize());
        //emit gwtChanged(QString::number(ui->tableView->getGWT()));
        qDebug()<<"height changed. => " << ui->totalHeight->text();
    }

}

void RockOutcrop::on_thickness_edited()
{

    ui->totalHeight->setText(QString::number(ui->tableView->totalHeight()));

    // move GWT to new position in UI
    double originalGWT = ui->tableView->getGWT();
    ui->gwtEdit->textChanged(QString::number(0));
    ui->gwtEdit->textChanged(QString::number(originalGWT));

    ui->reBtn->click();


}

void RockOutcrop::on_rowRemoved(int row)
{
    Q_UNUSED(row);
    // move GWT to new position
    double originalGWT = ui->tableView->getGWT();
    ui->gwtEdit->textChanged(QString::number(0));
    ui->gwtEdit->textChanged(QString::number(originalGWT));
}



void RockOutcrop::on_gwtEdit_textChanged(const QString &newGWT)
{
    ui->tableView->setGWT(newGWT.toDouble());
}


bool RockOutcrop::outputToJSON(QJsonObject &root)
{


    QString in;
    QFile inputFile(srtFileName);
    if(inputFile.open(QFile::ReadOnly)) {
    inputFile.open(QIODevice::ReadOnly | QIODevice::Text);
    in = inputFile.readAll();
    inputFile.close();
    }else{
        // if not input file provided add a default layer
        if(ui->tableView->m_sqlModel->rowCount()<1)
        {
            QList<QVariant> valueListRock;
            valueListRock << "Rock" << "-" << DefaultDensity << DefaultVs << DefaultEType << "-";
            ui->tableView->insertAt(valueListRock,0);
            ui->tableView->setTotalHeight(0);
            ui->totalHeight->setText("0");
            ui->totalLayerLineEdit->setText("1");
        }
    }

    QJsonDocument indoc = QJsonDocument::fromJson(in.toUtf8());
    //qWarning() << indoc.isNull();
    if (indoc.isNull())
    {
        qWarning() << "SRT.json is missing.";
        return false;
    }
    else{
        root = indoc.object();
        return true;
    }


}


void RockOutcrop::on_reBtn_clicked()
{

    BonzaTableModel* tableModel = ui->tableView->m_sqlModel;

    QWidget* FEMtab = ui->tabWidget->widget(0);

    double GWT = ui->tableView->getGWT();

    json root = {
        {"name","Configureation of Site Response Analysis of A Demo Site"},
        {"author","SimCenter Site Response Tool"}
    };
    QList<QString> listFEMtab = {"eSizeH", "eSizeV", "RockVs", "RockDen", "DashpotCoeff", "VisC", "GMPath"  };
    for (int i = 0; i < listFEMtab.size(); ++i) {
        QString edtName = listFEMtab[i] ;
        FEMtab->findChild<QLineEdit*>(edtName)->text();
    }
    json basicSettings;
    basicSettings["eSizeH"] = FEMtab->findChild<QLineEdit*>("eSizeH")->text().toDouble();
    basicSettings["eSizeV"] = FEMtab->findChild<QLineEdit*>("eSizeV")->text().toDouble();
    basicSettings["rockVs"] = FEMtab->findChild<QLineEdit*>("RockVs")->text().toDouble();
    basicSettings["rockDen"] = FEMtab->findChild<QLineEdit*>("RockDen")->text().toDouble();
    basicSettings["dashpotCoeff"] = FEMtab->findChild<QLineEdit*>("DashpotCoeff")->text().toDouble();
    basicSettings["dampingCoeff"] = FEMtab->findChild<QLineEdit*>("VisC")->text().toDouble();
    basicSettings["groundMotion"] = FEMtab->findChild<QLineEdit*>("GMPath")->text().toStdString();
    basicSettings["OpenSeesPath"] = FEMtab->findChild<QLineEdit*>("openseesPath")->text().toStdString();
    basicSettings["groundWaterTable"] = GWT;
    root["basicSettings"] = basicSettings;

    json soilProfile = { };

    json layer, soilLayers, material, materials;

    //QString FEMString = tableModel->record(index.row()).value("FEM").toString();
    int numLayers = tableModel->rowCount();

    for (int i=0; i<numLayers; i++)
    {
        QList<QVariant> list = tableModel->getRowInfo(i);
        std::istringstream iss(list.at(FEM-2).toString().toStdString());
        std::vector<std::string> pars((std::istream_iterator<std::string>(iss)),
                                      std::istream_iterator<std::string>());
        double eSize = atof(pars[0].c_str());
        int id = i;
        QStringList thisFEMList = list.at(FEM-2).toString().split(" ");
        int DrInd=0, hPermInd=0, vPermInd=0, uBulkInd=0;
        if(list.at(MATERIAL-2).toString()=="Elastic")
        {
            DrInd = 4; hPermInd = 6; vPermInd = 7; uBulkInd = 10;
        }else if(list.at(MATERIAL-2).toString()=="PM4Sand")
        {
            DrInd = 1; hPermInd = 25; vPermInd = 26; uBulkInd = 27;
        }

        if(!list.at(LAYERNAME-2).toString().toStdString().compare("Rock"))
        {
            double rockVsTmp = list.at(VS-2).toDouble();
            double rockDenTmp = list.at(DENSITY-2).toDouble();
            root["basicSettings"]["rockVs"] = rockVsTmp;
            root["basicSettings"]["rockDen"] = rockDenTmp;
        }

        layer = {
            {"id",id+1},
            {"name",list.at(LAYERNAME-2).toString().toStdString()},
            {"thickness",list.at(THICKNESS-2).toDouble()},
            {"density",list.at(DENSITY-2).toDouble()},
            {"vs",list.at(VS-2).toDouble()},
            {"material", id+1},
            {"color",list.at(COLOR-2).toString().toStdString()},
            {"eSize",eSize},
            {"Dr",thisFEMList.at(DrInd).toDouble()},
            {"hPerm",thisFEMList.at(hPermInd).toDouble()},
            {"vPerm",thisFEMList.at(vPermInd).toDouble()},
            {"uBulk",thisFEMList.at(uBulkInd).toDouble()},
        };
        material =  createMaterial(i+1, list.at(MATERIAL-2).toString().toStdString(),list.at(FEM-2).toString().toStdString());
        materials.push_back(material);
        soilLayers.push_back(layer);

    }







    soilProfile["soilLayers"] = soilLayers;

    root["soilProfile"]=soilProfile;
    root["materials"]=materials;

    // write prettified JSON to another file
    QString file_name = srtFileName;//"SRT.json";
    //QString file_name = QFileDialog::getSaveFileName(this, tr("Choose Path for saving the analysis"), "", tr("Config Files (*.json)"));

    if (!file_name.isNull())
    {
        std::ofstream o(file_name.toStdString());
        o << std::setw(4) << root << std::endl;
    } else {
        QMessageBox::information(this, "error", "Failed to get file name.");
    }



    mesher->mesh2DColumn();
    elementModel->clear();
    elementModel->setTotalHeight(ui->totalHeight->text().toDouble());
    elementModel->setNodes(mesher->nodes);

    //ElementModel* newElementModel = new ElementModel;
    for(std::vector<int>::size_type n = mesher->elements.size(); n > 0; n--)
    {
        //Quad *ele = it;
        int tag = mesher->elements[n-1]->tag();
        int i = mesher->elements[n-1]->i();
        int j = mesher->elements[n-1]->j();
        int k = mesher->elements[n-1]->k();
        int l = mesher->elements[n-1]->l();
        double t = mesher->elements[n-1]->thickness();
        QString color = QString::fromStdString(mesher->elements[n-1]->color());
        bool isActive = false;
        elementModel->addElement("quad",tag,i,j,k,l,t,color,isActive);
    }
    elementModel->refresh();

}

void RockOutcrop::on_runBtn_clicked()
{
    int numLayers = ui->totalLayerLineEdit->text().toInt();
    if (numLayers<=1)
    {
        QMessageBox::information(this,tr("Soil err"), "You need to add at least one soil layer.", tr("OK."));

    }
    else{
        QString openseespath =  theTabManager->openseespath();
        QString rockmotionpath =  theTabManager->rockmotionpath();
        bool openseesEmpty = openseespath=="" || openseespath=="Input the full path of OpenSees excutable.";
        bool rockEmpty = rockmotionpath=="" || rockmotionpath=="Input the path of a ground motion file.";
        if (openseesEmpty || rockEmpty)
        {
            QMessageBox::information(this,tr("Path error"), "You need to specify OpenSees' path and rock motion file's path in the configure tab.", tr("OK."));
        }else{
            // build tcl file
            ui->reBtn->click();
            SiteResponse *srt = new SiteResponse(srtFileName.toStdString(),
                                                 analysisDir.toStdString(),outputDir.toStdString());
            //QMessageBox::information(this,tr("Alert"), "Are you sure you have soil layers. If not, I'll quit.", tr("OK."));
            srt->buildTcl();

            if(!QDir(outputDir).exists())
                QDir().mkdir(outputDir);

            /*
            * Calling Opensee to do the work
            */
            //"/Users/simcenter/Codes/OpenSees/bin/opensees"
            //openseesProcess->start("/Users/simcenter/Codes/OpenSees/bin/opensees",QStringList()<<"/Users/simcenter/Codes/SimCenter/SiteResponseTool/bin/model.tcl");
            openseesProcess->start(openseespath,QStringList()<<tclName);
            openseesErrCount = 1;
            emit runBtnClicked(dinoView);
        }
    }

}

void RockOutcrop::onOpenSeesFinished()
{

    //writeSurfaceMotion();
    QString str_err = openseesProcess->readAllStandardError();

    if(openseesErrCount==1)
    {
        if(str_err.contains("Site response analysis is finished."))
        {
            QMessageBox::information(this,tr("OpenSees Information"), "Analysis is done.", tr("OK."));
            qDebug() << "opensees says:" << str_err;
            openseesErrCount = 2;
            theTabManager->getTab()->setCurrentIndex(2);
            theTabManager->setGMViewLoaded();
            theTabManager->reFreshGMTab();
            theTabManager->reFreshGMView();

            resultsTab->setCurrentIndex(1);

            postProcessor = new PostProcessor(outputDir);
            profiler->updatePostProcessor(postProcessor);
            theTabManager->updatePostProcessor(postProcessor);
            connect(postProcessor, SIGNAL(updateFinished()), profiler, SLOT(onPostProcessorUpdated()));
            postProcessor->update();

        }
    }

}



void RockOutcrop::writeSurfaceMotion()
{

}

json RockOutcrop::createMaterial(int tag, std::string matType, std::string parameters)
{
    json mat = {
        {"id", tag},
        {"type", matType}
    };
    std::istringstream iss(parameters);
    std::vector<std::string> pars((std::istream_iterator<std::string>(iss)),
                                     std::istream_iterator<std::string>());
    if (!matType.compare("Elastic"))
    {
        double eSize = atof(pars[0].c_str());
        double E = atof(pars[1].c_str());
        double poisson = atof(pars[2].c_str());
        double density = atof(pars[3].c_str());

        mat["E"] = E;
        mat["poisson"] = poisson;
        mat["density"] = density;
    } else if (!matType.compare("PM4Sand"))
    {
        mat["Dr"] = atof(pars[1].c_str());
        mat["G0"] = atof(pars[2].c_str());
        mat["hpo"] = atof(pars[3].c_str());
        mat["Den"] = atof(pars[4].c_str());
        mat["P_atm"] = atof(pars[5].c_str());
        mat["h0"] = atof(pars[6].c_str());
        mat["emax"] = atof(pars[7].c_str());
        mat["emin"] = atof(pars[8].c_str());
        mat["nb"] = atof(pars[9].c_str());
        mat["nd"] = atof(pars[10].c_str());
        mat["Ado"] = atof(pars[11].c_str());
        mat["z_max"] = atof(pars[12].c_str());
        mat["cz"] = atof(pars[13].c_str());
        mat["ce"] = atof(pars[14].c_str());
        mat["phic"] = atof(pars[15].c_str());
        mat["nu"] = atof(pars[16].c_str());
        mat["cgd"] = atof(pars[17].c_str());
        mat["cdr"] = atof(pars[18].c_str());

        mat["ckaf"] = atof(pars[19].c_str());
        mat["Q"] = atof(pars[20].c_str());
        mat["R"] = atof(pars[21].c_str());
        mat["m"] = atof(pars[22].c_str());
        mat["Fsed_min"] = atof(pars[23].c_str());
        mat["p_sedo"] = atof(pars[24].c_str());
    }
    return mat;
}

void RockOutcrop::refresh()
{
    //ui->tableView->move(0,50);
    QTime dieTime= QTime::currentTime().addMSecs(500);
    while (QTime::currentTime() < dieTime)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    //resize(ui->centralWidget->width(),ui->centralWidget->height()+1);



    /*
    QPropertyAnimation *animation1= new QPropertyAnimation(ui->tableView,"geometry");
    animation1->setDuration(500);
    animation1->setStartValue(ui->tableView->geometry());
    animation1->setEndValue(QRect(0, 0, ui->tableView->width(), ui->tableView->height()));
    animation1->start();
    */


    //ui->tableView->setVisible(true);


}




