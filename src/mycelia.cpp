/*
 * Mycelia immersive 3d network visualization tool.
 * Copyright (C) 2008-2010 Sean Whalen.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <IO/OpenFile.h>

#include <dataitem.hpp>
#include <graph.hpp>
#include <mycelia.hpp>
#include <vruihelp.hpp>
#include <generators/barabasigenerator.hpp>
#include <generators/erdosgenerator.hpp>
#include <generators/graphgenerator.hpp>
#include <generators/wattsgenerator.hpp>
#include <layout/arflayout.hpp>
#include <layout/arfwindow.hpp>
#include <layout/edgebundler.hpp>
#include <layout/frlayout.hpp>
#include <layout/graphlayout.hpp>
#include <parsers/chacoparser.hpp>
#include <parsers/dotparser.hpp>
#include <parsers/gmlparser.hpp>
#include <parsers/xmlparser.hpp>
#include <tools/graphbuilder.hpp>
#include <tools/nodeselector.hpp>
#include <windows/attributewindow.hpp>
#include <windows/imagewindow.hpp>
#include <Vrui/Tool.h>
#include <Vrui/DisplayState.h>
#include <GL/GLGeometryWrappers.h>
#ifdef __RPCSERVER__
#include "rpcserver.hpp"
#endif

using namespace std;

/*
 * link to gpu layout function if cuda is enabled
 */
#ifdef __CUDA__
#include <vector_types.h>
extern "C" { void gpuLayout(float4*, int*, int); }
#endif

/** Returns the base directory for resource files.
*
* Returns RESOURCEDIR if it exists or
* returns "." to search the current directory instead.
*/
std::string getResourceDir()
{
    std::string dir(RESOURCEDIR);

    try {
        IO::DirectoryPtr dirPtr = IO::openDirectory(dir.c_str());
    }
    catch (IO::Directory::OpenError e)
    {
        // This means you must run mycelia must exist in the CWD.
        dir = ".";
    }
    return dir;
}

Mycelia::Mycelia(int argc, char** argv, char** appDefaults)
    : Vrui::Application(argc, argv, appDefaults)
{
    // node layout / edge bundler
    dynamicLayout = new ArfLayout(this);
    staticLayout = new FruchtermanReingoldLayout(this);
    edgeBundler = new EdgeBundler(this);
    skipLayout = false;

    // node selection tool factory
    NodeSelectorFactory* selectorFactory = new NodeSelectorFactory(*Vrui::getToolManager(), this);
    Vrui::getToolManager()->addClass(selectorFactory, 0);

    // graph builder tool factory
    GraphBuilderFactory* builderFactory = new GraphBuilderFactory(*Vrui::getToolManager(), this);
    Vrui::getToolManager()->addClass(builderFactory, 0);

    // file submenu
    GLMotif::Popup* filePopup = new GLMotif::Popup("FilePopup", Vrui::getWidgetManager());
    GLMotif::SubMenu* fileSubMenu = new GLMotif::SubMenu("FileSubMenu", filePopup, false);

    GLMotif::Button* openFileButton = new GLMotif::Button("OpenFileButton", fileSubMenu, "Open...");
    openFileButton->getSelectCallbacks().add(this, &Mycelia::openFileCallback);

    GLMotif::Button* writeGraphButton = new GLMotif::Button("WriteGraphButton", fileSubMenu, "Save");
    writeGraphButton->getSelectCallbacks().add(this, &Mycelia::writeGraphCallback);

    // graph generators submenu
    GLMotif::Popup* generatorPopup = new GLMotif::Popup("GeneratorMenu", Vrui::getWidgetManager());
    generatorRadioBox = new GLMotif::RadioBox("GeneratorRadioBox", generatorPopup, false);
    generatorRadioBox->setSelectionMode(GLMotif::RadioBox::ATMOST_ONE);
    generatorRadioBox->getValueChangedCallbacks().add(this, &Mycelia::generatorCallback);

    erdosButton = new GLMotif::ToggleButton("ErdosButton", generatorRadioBox, "Random (Erdos-Renyi)");
    barabasiButton = new GLMotif::ToggleButton("BarabasiButton", generatorRadioBox, "Scale Free (Barabasi-Albert)");
    wattsButton = new GLMotif::ToggleButton("WattsButton", generatorRadioBox, "Small World (Watts-Strogatz)");

    // layout submenu
    GLMotif::Popup* layoutPopup = new GLMotif::Popup("LayoutPopup", Vrui::getWidgetManager());
    layoutRadioBox = new GLMotif::RadioBox("LayoutSubMenu", layoutPopup, false);
    layoutRadioBox->setSelectionMode(GLMotif::RadioBox::ALWAYS_ONE);
    layoutRadioBox->getValueChangedCallbacks().add(this, &Mycelia::resetLayoutCallback);

    staticButton = new GLMotif::ToggleButton("StaticButton", layoutRadioBox, "Static");
    dynamicButton = new GLMotif::ToggleButton("DynamicButton", layoutRadioBox, "Dynamic");
    layout = staticLayout;

    // render submenu
    GLMotif::Popup* renderPopup = new GLMotif::Popup("RenderPopup", Vrui::getWidgetManager());
    GLMotif::SubMenu* renderSubMenu = new GLMotif::SubMenu("RenderSubMenu", renderPopup, false);

    bundleButton = new GLMotif::ToggleButton("BundleButton", renderSubMenu, "Bundle Edges");
    bundleButton->getValueChangedCallbacks().add(this, &Mycelia::bundleCallback);

    nodeInfoButton = new GLMotif::ToggleButton("NodeInfoButton", renderSubMenu, "Show Node Information");
    nodeInfoButton->getValueChangedCallbacks().add(this, &Mycelia::nodeInfoCallback);

    nodeLabelButton = new GLMotif::ToggleButton("NodeLabelButton", renderSubMenu, "Show Node Labels");
    nodeLabelButton->setToggle(true);
    nodeLabelButton->getValueChangedCallbacks().add(this, &Mycelia::nodeLabelCallback);

    edgeLabelButton = new GLMotif::ToggleButton("EdgeLabelButton", renderSubMenu, "Show Edge Labels");
    edgeLabelButton->setToggle(true);
    edgeLabelButton->getValueChangedCallbacks().add(this, &Mycelia::nodeLabelCallback); // same callback

    componentButton = new GLMotif::ToggleButton("ComponentButton", renderSubMenu, "Show Only Selected Subgraph");
    componentButton->getValueChangedCallbacks().add(this, &Mycelia::componentCallback);

    // algorithms submenu
    GLMotif::Popup* algorithmsPopup = new GLMotif::Popup("AlgorithmsPopup", Vrui::getWidgetManager());
    GLMotif::SubMenu* algorithmsSubMenu = new GLMotif::SubMenu("AlgorithmsSubMenu", algorithmsPopup, false);

    shortestPathButton = new GLMotif::ToggleButton("ShortestPathButton", algorithmsSubMenu, "Shortest Path");
    shortestPathButton->getValueChangedCallbacks().add(this, &Mycelia::shortestPathCallback);

    spanningTreeButton = new GLMotif::ToggleButton("SpanningTreeButton", algorithmsSubMenu, "Spanning Tree");
    spanningTreeButton->getValueChangedCallbacks().add(this, &Mycelia::spanningTreeCallback);

    // plots submenu
    GLMotif::Popup* pythonPopup = new GLMotif::Popup("PythonPopup", Vrui::getWidgetManager());
    GLMotif::RadioBox* pythonSubMenu = new GLMotif::RadioBox("PythonSubMenu", pythonPopup, false);
    pythonSubMenu->setSelectionMode(GLMotif::RadioBox::ATMOST_ONE);
    pythonSubMenu->getValueChangedCallbacks().add(this, &Mycelia::pythonCallback);

    degreeButton = new GLMotif::ToggleButton("DegreeButton", pythonSubMenu, "Node Degree Distribution");
    centralityButton = new GLMotif::ToggleButton("CentralityButton", pythonSubMenu, "Node Betweenness Centrality");
    adjacencyButton = new GLMotif::ToggleButton("AdjacencyButton", pythonSubMenu, "Adjacency Matrix");
    lanetButton = new GLMotif::ToggleButton("LaNetButton", pythonSubMenu, "k-Core Hierarchical Layout");

    // main menu
    mainMenuPopup = new GLMotif::PopupMenu("MainMenuPopup", Vrui::getWidgetManager());
    mainMenuPopup->setTitle("Mycelia Network Visualizer");
    mainMenu = new GLMotif::Menu("MainMenu", mainMenuPopup, false);

    GLMotif::CascadeButton* fileCascade = new GLMotif::CascadeButton("FileCascade", mainMenu, "File");
    fileCascade->setPopup(filePopup);

    GLMotif::CascadeButton* generatorCascade = new GLMotif::CascadeButton("GeneratorCascade", mainMenu, "Generators");
    generatorCascade->setPopup(generatorPopup);

    GLMotif::CascadeButton* layoutCascade = new GLMotif::CascadeButton("LayoutCascade", mainMenu, "Layout");
    layoutCascade->setPopup(layoutPopup);

    GLMotif::CascadeButton* renderCascade = new GLMotif::CascadeButton("RenderCascade", mainMenu, "Rendering Options");
    renderCascade->setPopup(renderPopup);

    GLMotif::CascadeButton* algorithmsCascade = new GLMotif::CascadeButton("AlgorithmsCascade", mainMenu,  "Algorithms");
    algorithmsCascade->setPopup(algorithmsPopup);

    GLMotif::CascadeButton* pythonCascade = new GLMotif::CascadeButton("PythonCascade", mainMenu, "Python Plugins");
    pythonCascade->setPopup(pythonPopup);

    GLMotif::Button* clearButton = new GLMotif::Button("ClearButton", mainMenu, "Clear Screen");
    clearButton->getSelectCallbacks().add(this, &Mycelia::clearCallback);

    GLMotif::Button* navButton = new GLMotif::Button("NavButton", mainMenu, "Center Graph");
    navButton->getSelectCallbacks().add(this, &Mycelia::resetNavigationCallback);

    GLMotif::Button* layoutButton = new GLMotif::Button("LayoutButton", mainMenu, "Reset Layout");
    layoutButton->getSelectCallbacks().add(this, &Mycelia::resetLayoutCallback);

    fileSubMenu->manageChild();
    generatorRadioBox->manageChild();
    layoutRadioBox->manageChild();
    renderSubMenu->manageChild();
    algorithmsSubMenu->manageChild();
    pythonSubMenu->manageChild();
    mainMenu->manageChild();
    Vrui::setMainMenu(mainMenuPopup);

    // windows
    string dataDirectory(getResourceDir());
    dataDirectory += "/data";

    IO::DirectoryPtr dirPtr = IO::openDirectory(dataDirectory.c_str());

    fileWindow = new GLMotif::FileSelectionDialog(mainMenu->getManager(),
                    "Open file...", dirPtr, ".xml;.dot;.chaco;.gml");
    fileWindow->getOKCallbacks().add(this, &Mycelia::fileOpenAction);
    fileWindow->getCancelCallbacks().add(this, &Mycelia::fileCancelAction);

    nodeWindow = new AttributeWindow(this, "Node Attributes", 5);
    nodeWindow->hide();

    layoutWindow = new ArfWindow(this);
    layoutWindow->hide();

    imageWindow = new ImageWindow(this);
    imageWindow->hide();

    statusWindow = new AttributeWindow(this, "Status", 1);
    statusWindow->hide();

    // generators
    barabasiGenerator = new BarabasiGenerator(this);
    erdosGenerator = new ErdosGenerator(this);
    wattsGenerator = new WattsGenerator(this);
    generator = barabasiGenerator;

    // logo
    lastFrameTime = Vrui::getApplicationTime();
    rotationAngle = 0;
    rotationSpeed = 40.0;
    showingLogo = false;

    // parsers
    chacoParser = new ChacoParser(this);
    dotParser = new DotParser(this);
    gmlParser = new GmlParser(this);
    xmlParser = new XmlParser(this);

    // misc
    selectedNode = SELECTION_NONE;
    previousNode = SELECTION_NONE;
    coneAngle = 0.005;

    upVector = Vrui::getUpDirection();
    rightVector = Geometry::cross( Vrui::getForwardDirection(), upVector );

#ifdef __RPCSERVER__
    server = new RpcServer(this);
#endif

    // graph
    g = new Graph(this);
    gCopy = new Graph(this);

    // establishes initial node+edge sizes if graph builder is used first
    resetNavigationCallback(0);
}

Mycelia::~Mycelia()
{
    stopLayout();
}

void Mycelia::buildGraphList(MyceliaDataItem* dataItem) const
{
    // update version first in case of preemption
    dataItem->graphListVersion = gCopy->getVersion();

    glNewList(dataItem->nodeList, GL_COMPILE);
    gluSphere(dataItem->quadric, nodeRadius, 20, 20);
    glEndList();

    glNewList(dataItem->arrowList, GL_COMPILE);
    gluCylinder(dataItem->quadric, arrowWidth, 0.0, arrowHeight, 10, 1);

    gluQuadricOrientation(dataItem->quadric, GLU_INSIDE);
    gluDisk(dataItem->quadric, 0.0, arrowWidth, 10, 1);
    gluQuadricOrientation(dataItem->quadric, GLU_OUTSIDE);
    glEndList();

    glNewList(dataItem->graphList, GL_COMPILE);

    // Camera aligned texture nodes cannot be part of the display list since
    // we must readjust their orientation anytime we are rotating the graph.
    if (gCopy->getTextureNodeMode() == "align")
    {
        std::string filter = "image";
        drawNodes(dataItem, filter);
    }
    else
    {
        drawNodes(dataItem);
    }

    drawEdges(dataItem);
    glEndList();
}

void Mycelia::drawEdge(const Edge& edge, MyceliaDataItem* dataItem) const
{
    drawEdge(gCopy->getNodePosition(edge.source),
             gCopy->getNodePosition(edge.target),
             gCopy->getEdgeMaterialFromId(edge.material),
             edgeThickness * edge.weight,
             true,
             gCopy->isBidirectional(edge.source, edge.target),
             dataItem,
             getNodeEdgeOffset(edge.source, dataItem),
             getNodeEdgeOffset(edge.target, dataItem));
}

void Mycelia::drawEdge(const Vrui::Point& source,
                       const Vrui::Point& target,
                       const GLMaterial* material,
                       const Vrui::Scalar edgeThickness,
                       bool drawArrow,
                       bool isBidirectional,
                       MyceliaDataItem* dataItem,
                       double sourceEdgeOffset,
                       double targetEdgeOffset) const
{
    // computer graphics 2nd ed, p.413
    const Vrui::Vector edgeVector = target - source;
    const Vrui::Vector normalVector = Geometry::cross(edgeVector, upVector);
    const Vrui::Scalar length = Geometry::mag(edgeVector);

    // calculate space for directional arrow(s)
    double sourceOffset = sourceEdgeOffset;
    double targetOffset = length - sourceOffset - targetEdgeOffset;
    if (drawArrow)
    {
        // make room for the arrow head
        targetOffset -= edgeOffset;
    }

    if(isBidirectional && drawArrow)
    {
        sourceOffset += edgeOffset;
        targetOffset -= edgeOffset;
    }

    glMaterial(GLMaterialEnums::FRONT_AND_BACK, *material);

    glPushMatrix();

    // translate to point 1 and rotate towards point 2
    glTranslatef(source[0], source[1], source[2]);
    glRotatef(-VruiHelp::degrees(VruiHelp::angle(edgeVector, upVector)), normalVector[0], normalVector[1], normalVector[2]);

    // draw edge, leaving room for arrow
    glTranslatef(0, 0, sourceOffset);
    gluCylinder(dataItem->quadric, edgeThickness, edgeThickness, targetOffset, 10, 1);

    if(drawArrow)
    {
        // move near point 2 and draw arrow for this directed edge only.
        // if bidirectional, the other arrow will be drawn with that edge is drawn
        glTranslatef(0, 0, targetOffset);
        glCallList(dataItem->arrowList);
    }

    glPopMatrix();
}

void Mycelia::drawEdges(MyceliaDataItem* dataItem) const
{
    /*
    we don't draw an edge if one was already drawn between two nodes.
    this saves lots of time for very dense graphs.
    todo: the current approach to tracking drawn edges is a quick hack.
    */
    bool drawn[1000][1000] = {{false}};

    const GLMaterial *material;
    Vrui::Scalar width;

    foreach(int edge, gCopy->getEdges())
    {
        const Edge& e = gCopy->getEdge(edge);

        if(!isSelectedComponent(e.source) || drawn[e.source][e.target])
        {
            continue;
        }

        if(bundleButton->getToggle())
        {
            material = gCopy->getEdgeMaterial(edge);
            width = edgeThickness * e.weight;
            for(int segment = 0; segment <= edgeBundler->getSegmentCount(); segment++)
            {
                const Vrui::Point& p = *edgeBundler->getSegment(edge, segment);
                const Vrui::Point& q = *edgeBundler->getSegment(edge, segment + 1);
                drawEdge(p, q, material, width, false, false, dataItem);
            }
        }
        else
        {
            drawEdge(e, dataItem);
            drawn[e.source][e.target] = true;
        }
    }
}

void Mycelia::drawEdgeLabels(MyceliaDataItem* dataItem) const
{
    if(!edgeLabelButton->getToggle()) return;

    Vrui::Rotation inverseRotation = Vrui::getInverseNavigationTransformation().getRotation();

    // Fonts are drawn with up direction (0,1,0). So we need to rotate them to
    // Vrui's up direction which is not necessarily (0,0,1).
    Vrui::Vector fontUpVector = Vrui::Vector(0,1,0);
    Vrui::Scalar angle = VruiHelp::angle( fontUpVector, upVector );
    Vrui::Vector rotationAxis = Geometry::cross(fontUpVector, upVector);
    inverseRotation *= Vrui::Rotation(rotationAxis, angle);

    float scale = nodeRadius * FONT_MODIFIER;

    foreach(int edge, gCopy->getEdges())
    {
        if(!isSelectedComponent(gCopy->getEdge(edge).source))
        {
            continue;
        }

        const string& label = gCopy->getEdgeLabel(edge);

        if(label.size() > 0)
        {
            const Vrui::Point& p = VruiHelp::midpoint(gCopy->getSourceNodePosition(edge), gCopy->getTargetNodePosition(edge));

            glPushMatrix();
            glTranslatef(p[0] + nodeRadius, p[1] + nodeRadius, p[2] + nodeRadius);
            glRotate(inverseRotation);
            glScalef(scale, scale, scale);
            dataItem->font->Render(label.c_str());
            glPopMatrix();
        }
    }
}

void Mycelia::drawLogo(MyceliaDataItem* dataItem) const
{
    // Haven't figure out what Render() is changing...but unless we push
    // GL_TEXTURE_BIT, the rendered text disappears on the second call to
    // display() on some platforms (eg Linux on Intel Mac).
    glPushAttrib(GL_TEXTURE_BIT);

    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glPushMatrix();

    glTranslatef(-6.5, 0, 0);
    glRotate(rotationAngle, Vrui::Vector(1, 1, 1));
    glBegin(GL_TRIANGLE_STRIP);
    glColor3f(1, 1, 1); glVertex3f(1.5, 1.5, 1.5);
    glColor3f(1, 0, 0); glVertex3f(-1.5, -1.5, 1.5);
    glColor3f(0, 1, 0); glVertex3f(-1.5, 1.5, -1.5);
    glColor3f(0, 0, 1); glVertex3f(1.5, -1.5, -1.5);
    glColor3f(1, 1, 1); glVertex3f(1.5, 1.5, 1.5);
    glEnd();
    glPopMatrix();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glPushMatrix();
    glTranslatef(-4, 0, -1.5);
    glRotate(90.0, Vrui::Vector(1, 0, 0));
    glScalef(FONT_MODIFIER, FONT_MODIFIER, FONT_MODIFIER);
    dataItem->font->Render("mycelia.");
    glPopMatrix();

    glPopAttrib();

}

bool Mycelia::drawTextureNode(int node, MyceliaDataItem* dataItem) const
{
    std::string imagePath = gCopy->getNodeImagePath(node);

    std::pair<GLuint, std::pair<float, float> > texturePair = dataItem->getTextureId(imagePath);
    GLuint imageId = texturePair.first;
    float W = texturePair.second.first;
    float H = texturePair.second.second;

    if (imageId == 0) return false;

    const Vrui::NavTransform& inv = Vrui::getInverseNavigationTransformation();
    const Vrui::Rotation invRotation = inv.getRotation();

    // height is equal to node diameter
    float height = 2 * nodeRadius;
    float width = W / H * height;

    const Vrui::Point& p = gCopy->getNodePosition(node);

    // When we draw this, we center the image, so x is half the width.
    Vrui::Vector x(rightVector);
    x *= width / 2;
    Vrui::Vector y(upVector);
    y *= height / 2;

    glPushMatrix();

    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindTexture(GL_TEXTURE_2D, imageId);

    glTranslatef(p[0], p[1], p[2]);

    // Note: This does not allow the aspect ratio to change.
    // Note: Assuming an rightVector and upVector are perpendicular, this is
    //       equivalent to: (width, height) *= scale
    double scale = gCopy->getNodeImageScale(node);
    glScalef(scale,scale,scale);

    if (gCopy->getTextureNodeMode() == "align")
    {
        // textures will point in camera's up direction
        glRotate(invRotation);
    }

    Vrui::Point origin = Vrui::Point::origin;
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex(origin - x - y);
    glTexCoord2f(1, 0); glVertex(origin + x - y);
    glTexCoord2f(1, 1); glVertex(origin + x + y);
    glTexCoord2f(0, 1); glVertex(origin - x + y);
    glEnd();

    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_LIGHTING);
    glEnable(GL_CULL_FACE);

    glPopMatrix();

    return true;
}

bool Mycelia::drawShapeNode(int node, MyceliaDataItem* dataItem) const
{
    const Vrui::Point& p = gCopy->getNodePosition(node);
    const float size = gCopy->getNodeSize(node);

    if(node == highlightedNode)
    {
        glMaterial(GLMaterialEnums::FRONT_AND_BACK, *gCopy->getNodeMaterialFromId(MATERIAL_HIGHLIGHTED));
    }
    else if(node == selectedNode)
        glMaterial(GLMaterialEnums::FRONT_AND_BACK, *gCopy->getNodeMaterialFromId(MATERIAL_SELECTED));
    else if(node == previousNode)
        glMaterial(GLMaterialEnums::FRONT_AND_BACK, *gCopy->getNodeMaterialFromId(MATERIAL_SELECTED_PREVIOUS));
    else
        glMaterial(GLMaterialEnums::FRONT_AND_BACK, *gCopy->getNodeMaterial(node));

    glPushMatrix();
    glTranslatef(p[0], p[1], p[2]);
    glScalef(size, size, size);
    glCallList(dataItem->nodeList);
    glPopMatrix();

    return true;
}

void Mycelia::drawNode(int node, MyceliaDataItem* dataItem) const
{
    std::string type = gCopy->getNodeType(node);

    bool success = false;
    if (type == "image")
    {
        success = drawTextureNode(node, dataItem);
    }

    if (type == "shape" || !success)
    {
        success = drawShapeNode(node, dataItem);
    }
}

void Mycelia::drawNodes(MyceliaDataItem* dataItem, std::string filter) const
{
    std::string node_type;
    foreach(int node, gCopy->getNodes())
    {
        if(!isSelectedComponent(node))
        {
            continue;
        }

        node_type = gCopy->getNodeType(node);
        if (node_type != filter)
        {
            drawNode(node, dataItem);
        }
    }
}

void Mycelia::drawNodeLabels(MyceliaDataItem* dataItem) const
{
    if(!nodeLabelButton->getToggle()) return;

    Vrui::Rotation inverseRotation = Vrui::getInverseNavigationTransformation().getRotation();

    // Fonts are drawn with up direction (0,1,0). So we need to rotate them to
    // Vrui's up direction which is not necessarily (0,0,1).
    Vrui::Vector fontUpVector = Vrui::Vector(0,1,0);
    Vrui::Scalar angle = VruiHelp::angle( fontUpVector, upVector );
    Vrui::Vector rotationAxis = Geometry::cross(fontUpVector, upVector);
    inverseRotation *= Vrui::Rotation(rotationAxis, angle);

    float scale = nodeRadius * FONT_MODIFIER;

    foreach(int node, gCopy->getNodes())
    {
        if(!isSelectedComponent(node))
        {
            continue;
        }

        const Vrui::Point& p = gCopy->getNodePosition(node);
        const string& label = gCopy->getNodeLabel(node);

        if(label.size() > 0)
        {
            glPushMatrix();
            glTranslatef(p[0] + 1.1 * nodeRadius, p[1] + 1.1 * nodeRadius, p[2] + 1.1 * nodeRadius);
            glRotate(inverseRotation);
            glScalef(scale, scale, scale);

            // draw a shadow for readability
            glPushMatrix();
            glColor3f(0, 0, 0);
            glTranslatef(1, 0, -1);
            dataItem->font->Render(label.c_str());
            glPopMatrix();

            glColor3f(1, 1, 1);
            dataItem->font->Render(label.c_str());
            glPopMatrix();
        }
    }
}

void Mycelia::drawShortestPath(MyceliaDataItem* dataItem) const
{
    glMaterial(GLMaterialEnums::FRONT_AND_BACK, *gCopy->getNodeMaterialFromId(MATERIAL_SELECTED));

    for(int i = selectedNode; i != previousNode; i = predecessorVector[i])
    {
        if(i == predecessorVector[i]) break;

        drawNode(i, dataItem);
        Edge e(i, predecessorVector[i]);
        e.material = MATERIAL_SELECTED;
        drawEdge(e, const_cast<MyceliaDataItem*>(dataItem) );
    }
}

void Mycelia::drawSpanningTree(MyceliaDataItem* dataItem) const
{
    glMaterial(GLMaterialEnums::FRONT_AND_BACK, *gCopy->getNodeMaterialFromId(MATERIAL_SELECTED));

    for(int i = 0; i < (int)predecessorVector.size(); i++)
    {
        drawNode(i, dataItem);
        Edge e(i, predecessorVector[i]);
        drawEdge(e, const_cast<MyceliaDataItem*>(dataItem) );
    }
}

void Mycelia::display(GLContextData& contextData) const
{

    MyceliaDataItem* dataItem = contextData.retrieveDataItem<MyceliaDataItem>(this);

    if(showingLogo)
    {
        drawLogo(dataItem);
        return;
    }

    // re-create display list if it's been updated
    if(dataItem->graphListVersion != gCopy->getVersion())
    {
        buildGraphList(dataItem);
    }

    if(spanningTreeButton->getToggle())
    {
        drawSpanningTree(dataItem);
    }
    else
    {
        glCallList(dataItem->graphList);

        // Camera aligned texture nodes must be redrawn each time.
        // Rotatable texture nodes will be in the display list and thus
        // will rotate so long as we don't redraw the display list.
        if (gCopy->getTextureNodeMode() == "align")
        {
            std::string filter = "shape";
            drawNodes(dataItem, filter);
        }

        // Haven't figure out what FTGLTextureFont::Render() is changing...
        // but unless we push GL_TEXTURE_BIT, the rendered text disappears on
        // the second call to display() on some platforms
        // (eg Linux on Intel Mac).
        glDisable(GL_LIGHTING);
        glPushAttrib(GL_TEXTURE_BIT);
        drawNodeLabels(dataItem);
        drawEdgeLabels(dataItem);
        glPopAttrib();
        glEnable(GL_LIGHTING);

        if(shortestPathButton->getToggle())
        {
            drawShortestPath(dataItem);
        }
    }

}

double Mycelia::getNodeEdgeOffset(int node, MyceliaDataItem* dataItem) const
{
    // Determine an additional offset while drawing edges due to the node
    // being rendered as an texture which can have its own scale.

    double offset = nodeRadius;

    std::string type = gCopy->getNodeType(node);
    if (type == "image")
    {
        std::string imagePath = gCopy->getNodeImagePath(node);
        std::pair<GLuint, std::pair<float, float> > texturePair = dataItem->getTextureId(imagePath);
        GLuint imageId = texturePair.first;
        if (imageId != 0)
        {
            // The height is normalized to nodeDiameter = 2*nodeRadius when
            // imageScale = 1. So we use the height as the diameter of the sphere which edges
            // should end on. This gives a sphere inscribed in a cube
            // determined by the image's height. Assuming the right and up
            // vectors are perpendicular, the image dimensions W,H are
            // scaled during its drawing simply by imageScale. So the rendered
            // height in navigation coordiantes is just: nodeRadius * imageScale.
            // Dividing by two gives the radius which is our offset.
            double imageScale = gCopy->getNodeImageScale(node);
            offset = nodeRadius * imageScale;
            return offset;
        }
    }
    offset = nodeRadius * gCopy->getNodeSize(node);
    return offset;
}

void Mycelia::frame()
{
    double newFrameTime = Vrui::getApplicationTime();
    rotationAngle += (newFrameTime - lastFrameTime) * rotationSpeed;
    rotationAngle = Math::mod(rotationAngle, Vrui::Scalar(360));
    lastFrameTime = newFrameTime;

    g->lock();
    *gCopy = *g;
    g->unlock();

    if(gCopy->getNodeCount() == 0)
    {
        if (!showingLogo)
        {
            showingLogo = true;
            // disable user navigation
            Vrui::activateNavigationTool(reinterpret_cast<Vrui::Tool*>(this));
        }

        if (showingLogo)
        {
            // With the dummy navigation tool enabled, the navigation tool
            // tied to the device is blocked.  This other navigation tool
            // was actively updating the navigation transformation whenever
            // the window containing Vrui moved.  Since we are blocking this
            // tool, we must manually reset the navigation transformation.
            Vrui::setNavigationTransformation(Vrui::Point::origin, 30);

            // update the rotating tetrahedron in the logo
            Vrui::scheduleUpdate(Vrui::getApplicationTime()+0.02);
        }
    }
    else
    {
        showingLogo = false;

        // renable user navigation
        Vrui::deactivateNavigationTool(reinterpret_cast<Vrui::Tool*>(this));
    }

}

void Mycelia::initContext(GLContextData& contextData) const
{
    MyceliaDataItem* dataItem = new MyceliaDataItem();

    // fonts
    std::string fontDirectory(getResourceDir());
    fontDirectory += "/fonts";
    dataItem->font = new FTGLTextureFont((fontDirectory+"/Sansation_Light.ttf").c_str());
    dataItem->font->FaceSize(FONT_SIZE);

    contextData.addDataItem(this, dataItem);
}

bool Mycelia::isSelectedComponent(int node) const
{
    if(componentButton->getToggle())
    {
        return gCopy->getNodeComponent(node) == gCopy->getNodeComponent(selectedNode);
    }

    return true;
}

void Mycelia::setStatus(const char* status) const
{
    statusWindow->update("", status);

    if(strcmp(status, "") == 0)
    {
        statusWindow->hide();
    }
    else
    {
        statusWindow->show(true);
    }
}

/*
 * layout
 */
void Mycelia::resumeLayout() const
{
    // Resume only if dynamic and not skipping.

    // The reason we do not resume if the layout is static is because starting
    // a static layout is equivalent to running resetLayout, which is not
    // always desirable.
    if(layout->isDynamic() && !skipLayout)
    {
        startLayout();
    }
}

bool Mycelia::layoutIsStopped() const
{
    return layout->isStopped();
}

void Mycelia::setLayoutType(int type)
{
    if(type == LAYOUT_DYNAMIC)
    {
        edgeBundler->stop();
        bundleButton->setToggle(false);

        layoutRadioBox->setSelectedToggle(1);
        if (layout != dynamicLayout)
        {
            // Then we are switching layouts!
            stopLayout();
        }
        layout = dynamicLayout;
        layoutWindow->show();
    }
    else if(type == LAYOUT_STATIC)
    {
        layoutRadioBox->setSelectedToggle(0);
        if (layout != staticLayout)
        {
            // Then we are switching layouts!
            stopLayout();
        }
        layout = staticLayout;
        layoutWindow->hide();
    }
}

void Mycelia::setSkipLayout(bool skipLayout)
{
    this->skipLayout = skipLayout;
}

void Mycelia::startLayout() const
{
    layout->start();
}

void Mycelia::stopLayout() const
{
    edgeBundler->stop();
    staticLayout->stop();
    dynamicLayout->stop();
}

/*
 * callbacks
 */
void Mycelia::bundleCallback(GLMotif::ToggleButton::ValueChangedCallbackData* cbData)
{
    if(g->getNodeCount() == 0) return;

    if(cbData->set)
    {
        stopLayout();
        edgeBundler->start();
    }
    else
    {
        edgeBundler->stop();
        g->update();
        resumeLayout();
    }
}

void Mycelia::clearCallback(Misc::CallbackData* cbData)
{
    g->clear();

    // clear menu toggles
    bundleButton->setToggle(false);
    componentButton->setToggle(false);
    centralityButton->setToggle(false);
    degreeButton->setToggle(false);
    adjacencyButton->setToggle(false);
    lanetButton->setToggle(false);
    nodeInfoButton->setToggle(false);
    shortestPathButton->setToggle(false);
    spanningTreeButton->setToggle(false);
    barabasiButton->setToggle(false);
    erdosButton->setToggle(false);
    wattsButton->setToggle(false);

    // hide windows
    VruiHelp::hide(fileWindow);
    imageWindow->hide();
    nodeWindow->clear();
    nodeWindow->hide();
    statusWindow->clear();
    statusWindow->hide();
    generator->hide();
}

void Mycelia::componentCallback(GLMotif::ToggleButton::ValueChangedCallbackData* cbData)
{
    if(cbData->set)
    {
        g->setComponents();
    }

    resetLayoutCallback(0);
}

void Mycelia::fileCancelAction(GLMotif::FileSelectionDialog::CancelCallbackData* cbData)
{
    VruiHelp::hide(fileWindow);
}

void Mycelia::fileOpen(string &filename)
{
    // Note: endsWith() requires that filename not be const.

    // set to true if parser detects nodes with explicit positions
    skipLayout = false;

    // call appropriate parser
    if(VruiHelp::endsWith(filename, ".dot"))
    {
        dotParser->parse(filename);
    }
    else if(VruiHelp::endsWith(filename, ".xml"))
    {
        xmlParser->parse(filename);
    }
    else if(VruiHelp::endsWith(filename, ".chaco"))
    {
        chacoParser->parse(filename);
    }
    else if(VruiHelp::endsWith(filename, ".gml"))
    {
        gmlParser->parse(filename);
    }

    // reset navigation here in case skipLayout is true
    resetNavigationCallback(0);
    resetLayoutCallback(0);
}

void Mycelia::fileOpenAction(GLMotif::FileSelectionDialog::OKCallbackData* cbData)
{
    clearCallback(0);
    string filename = cbData->getSelectedPath();
    fileOpen(filename);

}

void Mycelia::generatorCallback(GLMotif::RadioBox::ValueChangedCallbackData* cbData)
{
    g->clear();

    setLayoutType(LAYOUT_DYNAMIC);
    generator->hide();

    if(cbData->newSelectedToggle == barabasiButton)
    {
        generator = barabasiGenerator;
    }
    else if(cbData->newSelectedToggle == erdosButton)
    {
        generator = erdosGenerator;
    }
    else if(cbData->newSelectedToggle == wattsButton)
    {
        generator = wattsGenerator;
    }

    generator->generate();
    resumeLayout();
}

void Mycelia::nodeInfoCallback(GLMotif::ToggleButton::ValueChangedCallbackData* cbData)
{
    if(cbData->set)
    {
        nodeWindow->show();
    }
    else
    {
        nodeWindow->hide();
    }
}

void Mycelia::nodeLabelCallback(GLMotif::ToggleButton::ValueChangedCallbackData* cbData)
{
    g->update();
}

void Mycelia::openFileCallback(Misc::CallbackData* cbData)
{
    VruiHelp::show(fileWindow, mainMenu);
}

void Mycelia::pythonCallback(GLMotif::RadioBox::ValueChangedCallbackData* cbData)
{
    if(g->getNodeCount() == 0) return;

    imageWindow->hide();

    if(cbData->newSelectedToggle == centralityButton)
    {
        vector<double> bc = gCopy->getBetweennessCentrality();
        ofstream out("/tmp/input.txt");

        foreach(int node, gCopy->getNodes())
        {
            out << bc[node] << endl;
        }

        out.close();
        imageWindow->load("python/plugins/bc.py");
    }
    else if(cbData->newSelectedToggle == degreeButton)
    {
        ofstream out("/tmp/input.txt");

        foreach(int node, gCopy->getNodes())
        {
            out << gCopy->getNodeDegree(node) << endl;
        }

        out.close();
        imageWindow->load("python/plugins/degree.py");
    }
    else if(cbData->newSelectedToggle == adjacencyButton)
    {
        bool a[1000][1000] = {{false}}; // temporary

        foreach(int source, gCopy->getNodes())
        {
            foreach(int target, gCopy->getNodes())
            {
                if(gCopy->hasEdge(source, target))
                {
                    a[source][target] = true;
                }
            }
        }

        ofstream out("/tmp/input.txt");

        for(int source = 0; source < gCopy->getNodeCount(); source++)
        {
            for(int target = 0; target < gCopy->getNodeCount(); target++)
            {
                out << a[source][target] << " ";
            }

            out << endl;
        }

        out.close();
        imageWindow->load("python/plugins/adjmatrix.py");
    }
    else if(cbData->newSelectedToggle == lanetButton)
    {
        ofstream out("/tmp/input.txt");

        foreach(int source, gCopy->getNodes())
        {
            foreach(int target, gCopy->getNodes())
            {
                if(gCopy->hasEdge(source, target))
                {
                    out << source << " " << target << endl;
                }
            }
        }

        out.close();
        imageWindow->load("python/plugins/lanet.py");
    }

    if(cbData->newSelectedToggle) imageWindow->show();
}

void Mycelia::resetLayoutCallback(Misc::CallbackData* cbData)
{
    resetLayout();
}

void Mycelia::resetLayout(bool watch)
{
    stopLayout();
    bundleButton->setToggle(false);

    // allow changing layout before graph is loaded
    if(staticButton->getToggle())
    {
        setLayoutType(LAYOUT_STATIC);
    }
    else
    {
        setLayoutType(LAYOUT_DYNAMIC);
    }

    // abort layout if no nodes, or positions hard coded in data
    int size = g->getNodeCount();

    if(skipLayout || size == 0)
    {
        return;
    }

    // reset layout state
    g->randomizePositions(100);
    g->clearVelocities();

    // In order to avoid a flicker during layout...let's recenter.
    if (watch)
    {
        // Note, this will reset the dynamic layout since it calls
        // resumeLayout(). Fortunately, the call to startLayout() (below)
        // is smart enough to handle this and not restart the thread.
        resetNavigationCallback(0);
    }

#ifndef __CUDA__
    // Some layouts will automatically call resetNavigationCallback once
    // they have finished laying out the graph.
    startLayout();
#else
    // positions
    float4* positions_h = new float4[size];

    foreach(int node, g->getNodes())
    {
        const Vrui::Point& p = g->getNodePosition(node);
        float4 q;
        q.x = p[0];
        q.y = p[1];
        q.z = p[2];
        q.w = g->getNodeDegree(node);
        positions_h[node] = q;
    }

    // adjacency matrix
    int* adjacencies_h = new int[size * size];

    for(int row = 0; row < size; row++)
    {
        for(int col = 0; col < size; col++)
        {
            adjacencies_h[row * size + col] = g->hasEdge(row, col);
        }
    }

    // layout
    gpuLayout(positions_h, adjacencies_h, size);

    // update positions
    foreach(int node, g->getNodes())
    {
        const float4& q = positions_h[node];
        g->setNodePosition(node, Vrui::Point(q.x, q.y, q.z));
    }

    // free memory
    delete[] positions_h;
    delete[] adjacencies_h;

    resetNavigationCallback(0);
#endif
}

void Mycelia::resetNavigationCallback(Misc::CallbackData* cbData)
{
    bool layoutWasRunning = !(layout->isStopped());
    stopLayout();

    pair<Vrui::Point, Vrui::Scalar> p = g->locate();
    Vrui::Point& center = p.first;
    Vrui::Scalar& radius = p.second;
    Vrui::Point shift(-center[0], -center[1], -center[2]);
    g->moveNodes(shift);

    nodeRadius = radius / 80;
    arrowHeight = nodeRadius / 2;
    arrowWidth = arrowHeight / 2;
    edgeThickness = nodeRadius / 7;
    edgeOffset = arrowHeight;

    Vrui::setNavigationTransformation(Vrui::Point::origin, radius);

    g->update();
    if (layoutWasRunning)
    {
        resumeLayout(); // for dynamic layout
    }
}

void Mycelia::shortestPathCallback(GLMotif::ToggleButton::ValueChangedCallbackData* cbData)
{
    if(shortestPathButton->getToggle())
    {
        if(previousNode != SELECTION_NONE && selectedNode != SELECTION_NONE)
        {
            predecessorVector = g->getShortestPath();
            Vrui::requestUpdate();
        }
        else
        {
            shortestPathButton->setToggle(false);
        }
    }
}

void Mycelia::spanningTreeCallback(GLMotif::ToggleButton::ValueChangedCallbackData* cbData)
{
    if(cbData->set)
    {
        predecessorVector = g->getSpanningTree();
        Vrui::requestUpdate();
    }
}

void Mycelia::writeGraphCallback(Misc::CallbackData* cbData)
{
    g->write("data/graphdump.dot");
}

/*
 * node selection
 */
void Mycelia::clearSelections()
{
    previousNode = selectedNode = SELECTION_NONE;
    g->update();
}

int Mycelia::getPreviousNode() const
{
    return previousNode;
}

int Mycelia::getSelectedNode() const
{
    return selectedNode;
}

void Mycelia::setSelectedNode(int node)
{
    if(!g->isValidNode(node))
    {
        cout << "invalid node selected: " << node << endl;
        return;
    }

    if(nodeInfoButton->getToggle())
    {
        nodeWindow->update(g->getNodeAttributes(node));
    }

    previousNode = selectedNode;
    selectedNode = node;

    shortestPathCallback(0);
    g->update();
#ifdef __RPCSERVER__
    server->callback(node);
#endif
}

void Mycelia::setHighlightedNode(int node)
{
    if (node != highlightedNode)
    {
        highlightedNode = node;
        g->update();
    }
}



int Mycelia::selectNode(const Vrui::Point& clickPosition) const
{
    std::pair<int, float> result = nearestNode(clickPosition);
    int nearest;

    // 8 seems to be a good scale that keep it useable.
    // This is absolutely arbitrary.
    if ( result.second <= Math::sqr(8*nodeRadius) )
    {
        nearest = result.first;
    }
    else
    {
        nearest = SELECTION_NONE;
    }
    return nearest;
}

int Mycelia::selectNode(const Vrui::Ray& ray) const
{
    int nearest = SELECTION_NONE;

    // Choosing an initial cone angle is difficult and depends on (at least)
    // the following factors:
    //
    //    1) the distance from the ray origin to the nodes
    //    2) the node radius
    //
    // Note that the zoom factor changes how far away the ray origin is. So,
    // take the ray and make tagent to some node of radius R.  The vector from
    // the ray origin to the center of the node forms an angle. This is our
    // desired angle. A consequence of this is that closer
    // nodes will need a larger steradian while farther away nodes need a
    // smaller steradian.  Making matters worse, the radius of each node can
    // be specified independently. All of this makes it difficult to select
    // a single best cone angle. Also, we need this to be fast as it is
    // calculated each time the cursor moves. Note: Our angle is specified
    // entirely in terms cross-sections, dot products, etc.  To make the
    // connection to steradians and the angle typically related to it, recall
    // that the angle is usually measured symmetrically about the ray. So
    // it would be necesary to cut our angle in half.
    //
    // Here is how can compromise: The center of the graph is kept at the
    // origin. So we can use the norm of the ray origin as broad measure for
    // the distance that each node in the graph is away from the origin.
    // We will also use the standard nodeRadius, essentially assuming we are
    // looking for standard sized nodes.  Finding larger nodes means you will
    // need to enter them before they are detected. Finding smaller nodes
    // means you will find them before your cursor actually touches the
    // circumference.  We know the hypotenuse (norm of the ray origin) and
    // the opposite leg (nodeRadius). So the angle we'd like is: arcsin(r/d).

    // When zoomed in quite a bit, the selection is off and you must go inside
    // the node in order for it to be selected. I'm not sure why this happens
    // exactly.  One thought is that I need to scale the radius with the zoom
    // factor.  However, this doesn't seem to be the case because the picking
    // adjusts properly within other zoom levels.
    float coneAngle2 = Math::asin( Math::sqr(nodeRadius) / Geometry::sqr(ray.getOrigin()) );
    float lambdaMin2 = numeric_limits<float>::max();

    foreach(int node, g->getNodes())
    {
        // vector pointing from origin to node position
        Vrui::Vector sp = g->getNodePosition(node) - ray.getOrigin();
        // squared dot product between sp and ray direction
        float x2 = Math::sqr(sp * ray.getDirection());

        // If the dot product is positive, then the node is in front of the
        // plane perpendicular to the ray direction.  Thus, the node is
        // in the general direction of the ray.  Generally, we want to balance
        // two traits:
        //
        //   1) the angle between sp and the ray direction
        //   2) the distance between the node and the ray origin
        //
        // For a fixed angle, minimizing the value of x gives us the nearest
        // node.  For a fixed value of x, minimizing the angle between sp and
        // the ray gives the nearest node. However, it is important to remember
        // that at any fixed value of x and angle, there is an entire circle
        // of points which are equally near the ray.
        //
        // We can think of a ray as an (uncountably) infinite collection of
        // points.  For argument's sake, consider a finite collection of points
        // along the ray direction.  For each point, we can find the node
        // nearest to said point.  A priori, there is no "obvious" choice
        // for how to choose amongst these nearest points. We must choose
        // how to define the nearest node.

        // One idea is to start with an initial max distance and then find
        // points closer to the ray origin such that their angle is smaller
        // than the angle that the current nearest point made with the ray.
        // However, this brings in ordering issues. If you happen to find a
        // really small angle immediately, then you could have another point
        // with a slightly larger angle that is much closer in distance to the
        // ray origin---this point would be excluded from being the nearest
        // node (undesirably).
        //
        // However, this seems like an acceptable trade-off given that we are
        // trying to hack a 3D operation onto a 2D device.  The initial max
        // angle is the coneAngle2.

        if (x2 >= 0 && x2 < lambdaMin2)
        {
            // This is the squared norm of cross product between vector from
            // origin to point and ray direction.  Even more importantly, this
            // quantity is: (a b sin \theta)^2
            float y2 = Geometry::sqr(Geometry::cross(sp, ray.getDirection()));

            // x2 is (a b cos \theta)^2. Thus, when we divide we get
            // (tan \theta)^2 which is monotonically increasing over -pi/2 to
            // pi/2, and thus, over the angles we care about (0 to 90 degrees).
            // So we can use it for comparisons instead of solving explicitly
            // for the angle.
            if (y2 / x2 <= coneAngle2)
            {
                nearest = node;
                lambdaMin2 = x2;
            }
        }
    }

    return nearest;
}

int Mycelia::selectNode(Vrui::InputDevice* device) const
{
    int nearest;

    if(device->is6DOFDevice())
    {
        Vrui::Point devicePosition(Vrui::getNavigationTransformation().inverseTransform(device->getPosition()));
        nearest = selectNode(devicePosition);
    }
    else
    {
        Vrui::Ray deviceRay(device->getPosition(), device->getRayDirection());
        deviceRay.transform(Vrui::getInverseNavigationTransformation());
        deviceRay.normalizeDirection();
        nearest = selectNode(deviceRay);
    }
    return nearest;
}

std::pair<int, float> Mycelia::nearestNode(const Vrui::Point& clickPosition) const
{
    int nearest = SELECTION_NONE;
    float minDist2 = numeric_limits<float>::max();

    foreach(int node, g->getNodes())
    {
        float dist2 = Geometry::sqrDist(clickPosition, g->getNodePosition(node));

        if(dist2 < minDist2)
        {
            nearest = node;
            minDist2 = dist2;
        }
    }

    std::pair<int, float> result(nearest, minDist2);
    return result;
}

Vrui::Scalar Mycelia::getArrowWidth() const
{
    return arrowWidth;
}

Vrui::Scalar Mycelia::getArrowHeight() const
{
    return arrowHeight;
}

Vrui::Scalar Mycelia::getEdgeThickness() const
{
    return edgeThickness;
}

int main(int argc, char** argv)
{
    char** appDefaults = 0;
    Mycelia app(argc, argv, appDefaults);
    app.run();

    return 0;
}
