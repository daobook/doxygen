/******************************************************************************
 *
 * Copyright (C) 1997-2020 by Dimitri van Heesch.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation under the terms of the GNU General Public License is hereby
 * granted. No representations are made about the suitability of this software
 * for any purpose. It is provided "as is" without express or implied warranty.
 * See the GNU General Public License for more details.
 *
 * Documents produced by Doxygen are derivative works derived from the
 * input used in their production; they are not affected by this license.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <fstream>
#include <algorithm>

#include "diagram.h"
#include "image.h"
#include "classdef.h"
#include "config.h"
#include "message.h"
#include "util.h"
#include "doxygen.h"
#include "portable.h"
#include "index.h"
#include "classlist.h"
#include "textstream.h"

//-----------------------------------------------------------------------------

class TreeDiagram;
class DiagramItem;
using DiagramItemList = std::vector<DiagramItem*>;

/** Class representing a single node in the built-in class diagram */
class DiagramItem
{
  public:
    DiagramItem(DiagramItem *p,uint number,const ClassDef *cd,
                Protection prot,Specifier virt,const QCString &ts);
    QCString label() const;
    QCString fileName() const;
    DiagramItem *parentItem() { return m_parent; }
    DiagramItemList getChildren() { return m_children; }
    void move(int dx,int dy) { m_x+=(uint)dx; m_y+=(uint)dy; }
    uint xPos() const { return m_x; }
    uint yPos() const { return m_y; }
    uint avgChildPos() const;
    uint numChildren() const;
    void addChild(DiagramItem *di);
    uint number() const { return m_num; }
    Protection protection() const { return m_prot; }
    Specifier virtualness() const { return m_virt; }
    void putInList() { m_inList=TRUE; }
    bool isInList() const { return m_inList; }
    const ClassDef *getClassDef() const { return m_classDef; }
  private:
    DiagramItemList m_children;
    DiagramItem *m_parent;
    uint m_x = 0;
    uint m_y = 0;
    uint m_num;
    Protection m_prot;
    Specifier m_virt;
    QCString m_templSpec;
    bool m_inList = false;
    const ClassDef *m_classDef;
};

/** Class representing a row in the built-in class diagram */
class DiagramRow
{
  public:
    using Ptr = std::unique_ptr<DiagramItem>;
    using Vec = std::vector<Ptr>;
    using iterator = typename Vec::iterator;
    using reverse_iterator = typename Vec::reverse_iterator;
    DiagramRow(TreeDiagram *d,uint l) : m_diagram(d), m_level(l) {}
    void insertClass(DiagramItem *parent,const ClassDef *cd,bool doBases,
                     Protection prot,Specifier virt,const QCString &ts);
    uint number() { return m_level; }

    DiagramItem *item(int index) { return m_items.at(index).get(); }
    uint numItems() const { return static_cast<uint>(m_items.size()); }
    iterator begin() { return m_items.begin();  }
    iterator end()   { return m_items.end();    }
    reverse_iterator rbegin() { return m_items.rbegin();  }
    reverse_iterator rend()   { return m_items.rend();    }
  private:
    TreeDiagram *m_diagram;
    uint m_level;
    Vec m_items;
};

/** Class representing the tree layout for the built-in class diagram. */
class TreeDiagram
{
  public:
    using Ptr = std::unique_ptr<DiagramRow>;
    using Vec = std::vector<Ptr>;
    using iterator = typename Vec::iterator;
    TreeDiagram(const ClassDef *root,bool doBases);
    void computeLayout();
    uint computeRows();
    void moveChildren(DiagramItem *root,int dx);
    void computeExtremes(uint *labelWidth,uint *xpos);
    void drawBoxes(TextStream &t,Image *image,
                   bool doBase,bool bitmap,
                   uint baseRows,uint superRows,
                   uint cellWidth,uint cellHeight,
                   QCString relPath="",
                   bool generateMap=TRUE);
    void drawConnectors(TextStream &t,Image *image,
                   bool doBase,bool bitmap,
                   uint baseRows,uint superRows,
                   uint cellWidth,uint cellheight);
    DiagramRow *row(int index) { return m_rows.at(index).get(); }
    uint numRows() const { return static_cast<uint>(m_rows.size()); }
    DiagramRow *addRow(uint l)
    { m_rows.push_back(std::make_unique<DiagramRow>(this,l)); return m_rows.back().get(); }
    iterator begin() { return m_rows.begin();  }
    iterator end()   { return m_rows.end();    }
  private:
    bool layoutTree(DiagramItem *root,uint row);
    TreeDiagram &operator=(const TreeDiagram &);
    TreeDiagram(const TreeDiagram &);
    Vec m_rows;
};



//-----------------------------------------------------------------------------

const uint maxTreeWidth = 8;
const uint gridWidth  = 100;
const uint gridHeight = 100;

const uint labelHorSpacing  = 10;  // horizontal distance between labels
const uint labelVertSpacing = 32;  // vertical distance between labels
const uint labelHorMargin   = 6;   // horiz. spacing between label and box
const uint fontHeight       = 12;  // height of a character

static uint protToMask(Protection p)
{
  switch(p)
  {
    case Public:    return 0xffffffff;
    case Package: // package is not possible!
    case Protected: return 0xcccccccc;
    case Private:   return 0xaaaaaaaa;
  }
  return 0;
}

static uchar protToColor(Protection p)
{
  switch(p)
  {
    case Public:    return 6;
    case Package: // package is not possible!
    case Protected: return 5;
    case Private:   return 4;
  }
  return 0;
}

static QCString protToString(Protection p)
{
  switch(p)
  {
    case Public:    return "solid";
    case Package: // package is not possible!
    case Protected: return "dashed";
    case Private:   return "dotted";
  }
  return QCString();
}

static uint virtToMask(Specifier p)
{
  switch(p)
  {
    case Normal:    return 0xffffffff;
    case Virtual:   return 0xf0f0f0f0;
    default:        return 0;
  }
  return 0;
}

// pre: dil is not empty
static Protection getMinProtectionLevel(const DiagramItemList &dil)
{
  auto it = dil.begin();
  Protection result = Private;
  if (it!=dil.end())
  {
    result=(*it)->protection();
    for (++it;it!=dil.end();++it)
    {
      Protection p=(*it)->protection();
      if (p!=result)
      {
        if (result==Protected && p==Public) result=p;
        else if (result==Private) result=p;
      }
    }
  }
  return result;
}

static void writeBitmapBox(DiagramItem *di,Image *image,
                           uint x,uint y,uint w,uint h,bool firstRow,
                           bool hasDocs,bool children=FALSE)
{
  uchar colFill = hasDocs ? (firstRow ? 0 : 2) : 7;
  uchar colBorder = (firstRow || !hasDocs) ? 1 : 3;
  uint l = Image::stringLength(di->label());
  uint mask=virtToMask(di->virtualness());
  image->fillRect(x+1,y+1,w-2,h-2,colFill,mask);
  image->drawRect(x,y,w,h,colBorder,mask);
  image->writeString(x+(w-l)/2, y+(h-fontHeight)/2, di->label(),1);
  if (children)
  {
    uint i;
    for (i=0;i<5;i++)
    {
      image->drawHorzLine(y+h+i-6,x+w-2-i,x+w-2,firstRow?1:3,0xffffffff);
    }
  }
}

static void writeVectorBox(TextStream &t,DiagramItem *di,
                           float x,float y,bool children=FALSE)
{
  if (di->virtualness()==Virtual) t << "dashed\n";
  t << " (" << convertToPSString(di->label()) << ") " << x << " " << y << " box\n";
  if (children) t << x << " " << y << " mark\n";
  if (di->virtualness()==Virtual) t << "solid\n";
}

static void writeMapArea(TextStream &t,const ClassDef *cd,QCString relPath,
                         uint x,uint y,uint w,uint h)
{
  if (cd->isLinkable())
  {
    QCString ref=cd->getReference();
    t << "<area ";
    if (!ref.isEmpty())
    {
      t << externalLinkTarget(true);
    }
    t << "href=\"";
    t << externalRef(relPath,ref,TRUE);
    t << addHtmlExtensionIfMissing(cd->getOutputFileBase());
    if (!cd->anchor().isEmpty())
    {
      t << "#" << cd->anchor();
    }
    t << "\" ";
    QCString tooltip = cd->briefDescriptionAsTooltip();
    if (!tooltip.isEmpty())
    {
      t << "title=\"" << convertToHtml(tooltip) << "\" ";
    }
    t << "alt=\"" << convertToXML(cd->displayName());
    t << "\" shape=\"rect\" coords=\"" << x << "," << y << ",";
    t << (x+w) << "," << (y+h) << "\"/>\n";
  }
}
//-----------------------------------------------------------------------------

DiagramItem::DiagramItem(DiagramItem *p,uint number,const ClassDef *cd,
                         Protection pr,Specifier vi,const QCString &ts)
  : m_parent(p), m_num(number), m_prot(pr), m_virt(vi), m_templSpec(ts), m_classDef(cd)
{
}

QCString DiagramItem::label() const
{
  QCString result;
  if (!m_templSpec.isEmpty())
  {
    // we use classDef->name() here and not displayName() in order
    // to get the name used in the inheritance relation.
    QCString n = m_classDef->name();
    if (/*n.right(2)=="-g" ||*/ n.right(2)=="-p")
    {
      n = n.left(n.length()-2);
    }
    result=insertTemplateSpecifierInScope(n,m_templSpec);
  }
  else
  {
    result=m_classDef->displayName();
  }
  if (Config_getBool(HIDE_SCOPE_NAMES)) result=stripScope(result);
  return result;
}

QCString DiagramItem::fileName() const
{
  return m_classDef->getOutputFileBase();
}

uint DiagramItem::avgChildPos() const
{
  DiagramItem *di;
  size_t c=m_children.size();
  if (c==0) // no children -> don't move
    return xPos();
  if ((di=m_children.front())->isInList()) // children should be in a list
    return di->xPos();
  if (c&1) // odd number of children -> get pos of middle child
    return m_children.at(c/2)->xPos();
  else // even number of children -> get middle of most middle children
    return (m_children.at(c/2-1)->xPos()+m_children.at(c/2)->xPos())/2;
}

uint DiagramItem::numChildren() const
{
  return static_cast<uint>(m_children.size());
}

void DiagramItem::addChild(DiagramItem *di)
{
  m_children.push_back(di);
}

//---------------------------------------------------------------------------

void DiagramRow::insertClass(DiagramItem *parent,const ClassDef *cd,bool doBases,
                             Protection prot,Specifier virt,const QCString &ts)
{
  auto di = std::make_unique<DiagramItem>(parent, m_diagram->row(m_level)->numItems(),
                                          cd,prot,virt,ts);
  DiagramItem *di_ptr = di.get();
  if (parent) parent->addChild(di_ptr);
  di->move((int)(m_items.size()*gridWidth),(int)(m_level*gridHeight));
  m_items.push_back(std::move(di));
  int count=0;
  for (const auto &bcd : doBases ? cd->baseClasses() : cd->subClasses())
  {
    /* there are base/sub classes */
    ClassDef *ccd=bcd.classDef;
    if (ccd && ccd->isVisibleInHierarchy()) count++;
  }
  if (count>0 && (prot!=Private || !doBases))
  {
    DiagramRow *row=0;
    if (m_diagram->numRows()<=m_level+1) /* add new row */
    {
      row=m_diagram->addRow(m_level+1);
    }
    else /* get next row */
    {
      row=m_diagram->row(m_level+1);
    }
    for (const auto &bcd : doBases ? cd->baseClasses() : cd->subClasses())
    {
      ClassDef *ccd=bcd.classDef;
      if (ccd && ccd->isVisibleInHierarchy())
      {
        row->insertClass(di_ptr,ccd,doBases,bcd.prot,
            doBases?bcd.virt:Normal,
            doBases?bcd.templSpecifiers:QCString());
      }
    }
  }
}

//---------------------------------------------------------------------------

TreeDiagram::TreeDiagram(const ClassDef *root,bool doBases)
{
  auto row = std::make_unique<DiagramRow>(this,0);
  DiagramRow *row_ptr = row.get();
  m_rows.push_back(std::move(row));
  row_ptr->insertClass(0,root,doBases,Public,Normal,QCString());
}

void TreeDiagram::moveChildren(DiagramItem *root,int dx)
{
  for (const auto &di : root->getChildren())
  {
    di->move(dx,0);
    moveChildren(di,dx);
  }
}

bool TreeDiagram::layoutTree(DiagramItem *root,uint r)
{
  bool moved=FALSE;
  //printf("layoutTree(%s,%d)\n",qPrint(root->label()),r);

  if (root->numChildren()>0)
  {
    auto children = root->getChildren();
    uint k;
    uint pPos=root->xPos();
    uint cPos=root->avgChildPos();
    if (pPos>cPos) // move children
    {
      const auto &row=m_rows.at(r+1);
      //printf("Moving children %d-%d in row %d\n",
      //    dil->getFirst()->number(),row->count()-1,r+1);
      for (k=children.front()->number();k<row->numItems();k++)
        row->item(k)->move((int)(pPos-cPos),0);
      moved=TRUE;
    }
    else if (pPos<cPos) // move parent
    {
      const auto &row=m_rows.at(r);
      //printf("Moving parents %d-%d in row %d\n",
      //    root->number(),row->count()-1,r);
      for (k=root->number();k<row->numItems();k++)
        row->item(k)->move((int)(cPos-pPos),0);
      moved=TRUE;
    }

    // recurse to children
    auto it = children.begin();
    for (;it!=children.end() && !moved && !(*it)->isInList();++it)
    {
      moved = layoutTree(*it,r+1);
    }
  }
  return moved;
}

void TreeDiagram::computeLayout()
{
  auto it = m_rows.begin();
  while (it!=m_rows.end() && (*it)->numItems()<maxTreeWidth) ++it;
  if (it!=m_rows.end())
  {
    const auto &row = *it;
    //printf("computeLayout() list row at %d\n",row->number());
    DiagramItem *opi=0;
    int delta=0;
    bool first=TRUE;
    for (const auto &di : *row)
    {
      DiagramItem *pi=di->parentItem();
      if (pi==opi && !first) { delta-=gridWidth; }
      first = pi!=opi;
      opi=pi;
      di->move(delta,0); // collapse all items in the same
                         // list (except the first)
      di->putInList();
    }
  }

  // re-organize the diagram items
  DiagramItem *root=m_rows.front()->item(0);
  while (layoutTree(root,0)) { }

  // move first items of the lists
  if (it!=m_rows.end())
  {
    const auto &row = *it;
    auto rit = row->begin();
    while (rit!=row->end())
    {
      DiagramItem *pi=(*rit)->parentItem();
      if (pi->numChildren()>1)
      {
        (*rit)->move(gridWidth,0);
        while (rit!=row->end() && (*rit)->parentItem()==pi)
        {
          ++rit;
        }
      }
      else
      {
        ++rit;
      }
    }
  }
}

uint TreeDiagram::computeRows()
{
  //printf("TreeDiagram::computeRows()=%d\n",count());
  uint count=0;
  auto it = m_rows.begin();
  while (it!=m_rows.end() && !(*it)->item(0)->isInList())
  {
    ++it;
    ++count;
  }

  //printf("count=%d row=%p\n",count,row);
  if (it!=m_rows.end())
  {
    const auto &row = *it;
    uint maxListLen=0;
    uint curListLen=0;
    DiagramItem *opi=0;
    for (const auto &di : *row) // for each item in a row
    {
      if (di->parentItem()!=opi) curListLen=1; else curListLen++;
      if (curListLen>maxListLen) maxListLen=curListLen;
      opi=di->parentItem();
    }
    //printf("maxListLen=%d\n",maxListLen);
    count+=maxListLen;
  }
  return count;
}

void TreeDiagram::computeExtremes(uint *maxLabelLen,uint *maxXPos)
{
  uint ml=0,mx=0;
  for (const auto &dr : m_rows) // for each row
  {
    bool done=FALSE;
    for (const auto &di : *dr) // for each item in a row
    {
      if (di->isInList()) done=TRUE;
      if (maxXPos) mx=std::max(mx,(uint)di->xPos());
      if (maxLabelLen) ml=std::max(ml,Image::stringLength(di->label()));
    }
    if (done) break;
  }
  if (maxLabelLen) *maxLabelLen=ml;
  if (maxXPos)     *maxXPos=mx;
}

//! helper class representing an iterator that can iterate forwards or backwards
template<class C,class I>
class DualDirIterator
{
  public:
    DualDirIterator(C &container,bool fwd)
      : m_container(container), m_forward(fwd)
    {
      if (fwd) m_it = container.begin();
      else m_rit = container.rbegin();
    }
    void operator++()
    {
      if (m_forward) ++m_it++; else ++m_rit;
    }
    I &operator*()
    {
      return m_forward ? *m_it : *m_rit;
    }

    bool atEnd()
    {
      if (m_forward)
        return m_it==m_container.end();
      else
        return m_rit==m_container.rend();
    }

  private:
    C &m_container;
    bool m_forward;
    typename C::iterator m_it;
    typename C::reverse_iterator m_rit;
};

void TreeDiagram::drawBoxes(TextStream &t,Image *image,
                            bool doBase,bool bitmap,
                            uint baseRows,uint superRows,
                            uint cellWidth,uint cellHeight,
                            QCString relPath,
                            bool generateMap)
{
  auto it = m_rows.begin();
  if (it!=m_rows.end() && !doBase) ++it;
  bool firstRow = doBase;
  bool done=FALSE;
  for (;it!=m_rows.end() && !done;++it) // for each row
  {
    const auto &dr = *it;
    uint x=0,y=0;
    float xf=0.0f,yf=0.0f;
    DiagramItem *firstDi = dr->item(0);
    if (firstDi->isInList()) // put boxes in a list
    {
      DiagramItem *opi=0;
      DualDirIterator<DiagramRow,const std::unique_ptr<DiagramItem>&> dit(*dr,!doBase);
      while (!dit.atEnd())
      {
        DiagramItem *di = (*dit).get();
        if (di->parentItem()==opi)
        {
          if (bitmap)
          {
            if (doBase) y -= cellHeight+labelVertSpacing;
            else        y += cellHeight+labelVertSpacing;
          }
          else
          {
            if (doBase) yf += 1.0f;
            else        yf -= 1.0f;
          }
        }
        else
        {
          if (bitmap)
          {
            x = di->xPos()*(cellWidth+labelHorSpacing)/gridWidth;
            if (doBase)
            {
              y = image->height()-
                superRows*cellHeight-
                (superRows-1)*labelVertSpacing-
                di->yPos()*(cellHeight+labelVertSpacing)/gridHeight;
            }
            else
            {
              y = (baseRows-1)*(cellHeight+labelVertSpacing)+
                di->yPos()*(cellHeight+labelVertSpacing)/gridHeight;
            }
          }
          else
          {
            xf = di->xPos()/(float)gridWidth;
            if (doBase)
            {
              yf = di->yPos()/(float)gridHeight+superRows-1;
            }
            else
            {
              yf = superRows-1-di->yPos()/(float)gridHeight;
            }
          }
        }
        opi=di->parentItem();

        if (bitmap)
        {
          bool hasDocs=di->getClassDef()->isLinkable();
          writeBitmapBox(di,image,x,y,cellWidth,cellHeight,firstRow,
              hasDocs,di->numChildren()>0);
          if (!firstRow && generateMap)
            writeMapArea(t,di->getClassDef(),relPath,x,y,cellWidth,cellHeight);
        }
        else
        {
          writeVectorBox(t,di,xf,yf,di->numChildren()>0);
        }

        ++dit;
      }
      done=TRUE;
    }
    else // draw a tree of boxes
    {
      for (const auto &di : *dr)
      {
        if (bitmap)
        {
          x = di->xPos()*(cellWidth+labelHorSpacing)/gridWidth;
          if (doBase)
          {
            y = image->height()-
              superRows*cellHeight-
              (superRows-1)*labelVertSpacing-
              di->yPos()*(cellHeight+labelVertSpacing)/gridHeight;
          }
          else
          {
            y = (baseRows-1)*(cellHeight+labelVertSpacing)+
              di->yPos()*(cellHeight+labelVertSpacing)/gridHeight;
          }
          bool hasDocs=di->getClassDef()->isLinkable();
          writeBitmapBox(di.get(),image,x,y,cellWidth,cellHeight,firstRow,hasDocs);
          if (!firstRow && generateMap)
            writeMapArea(t,di->getClassDef(),relPath,x,y,cellWidth,cellHeight);
        }
        else
        {
          xf=di->xPos()/(float)gridWidth;
          if (doBase)
          {
            yf = di->yPos()/(float)gridHeight+superRows-1;
          }
          else
          {
            yf = superRows-1-di->yPos()/(float)gridHeight;
          }
          writeVectorBox(t,di.get(),xf,yf);
        }
      }
    }
    firstRow=FALSE;
  }
}

void TreeDiagram::drawConnectors(TextStream &t,Image *image,
                                 bool doBase,bool bitmap,
                                 uint baseRows,uint superRows,
                                 uint cellWidth,uint cellHeight)
{
  bool done=FALSE;
  auto it = m_rows.begin();
  for (;it!=m_rows.end() && !done;++it) // for each row
  {
    const auto &dr = *it;
    DiagramItem *rootDi = dr->item(0);
    if (rootDi->isInList()) // row consists of list connectors
    {
      uint x=0,y=0,ys=0;
      float xf=0.0f,yf=0.0f,ysf=0.0f;
      auto rit = dr->begin();
      while (rit!=dr->end())
      {
        DiagramItem *di=(*rit).get();
        DiagramItem *pi=di->parentItem();
        DiagramItemList dil=pi->getChildren();
        DiagramItem *last=dil.back();
        if (di==last) // single child
        {
          if (bitmap) // draw pixels
          {
            x = di->xPos()*(cellWidth+labelHorSpacing)/gridWidth + cellWidth/2;
            if (doBase) // base classes
            {
              y = image->height()-
                (superRows-1)*(cellHeight+labelVertSpacing)-
                di->yPos()*(cellHeight+labelVertSpacing)/gridHeight;
              image->drawVertArrow(x,y,y+labelVertSpacing/2,
                                   protToColor(di->protection()),
                                   protToMask(di->protection()));
            }
            else // super classes
            {
              y = (baseRows-1)*(cellHeight+labelVertSpacing)-
                labelVertSpacing/2+
                di->yPos()*(cellHeight+labelVertSpacing)/gridHeight;
              image->drawVertLine(x,y,y+labelVertSpacing/2,
                                  protToColor(di->protection()),
                                  protToMask(di->protection()));
            }
          }
          else // draw vectors
          {
            t << protToString(di->protection()) << "\n";
            if (doBase)
            {
              t << "1 " << (di->xPos()/(float)gridWidth) << " "
                << (di->yPos()/(float)gridHeight+superRows-1) << " in\n";
            }
            else
            {
              t << "0 " << (di->xPos()/(float)gridWidth) << " "
                << ((float)superRows-0.25f-di->yPos()/(float)gridHeight)
                << " in\n";
            }
          }
        }
        else // multiple children, put them in a vertical list
        {
          if (bitmap)
          {
            x = di->parentItem()->xPos()*
              (cellWidth+labelHorSpacing)/gridWidth+cellWidth/2;
            if (doBase) // base classes
            {
              ys = image->height()-
                (superRows-1)*(cellHeight+labelVertSpacing)-
                di->yPos()*(cellHeight+labelVertSpacing)/gridHeight;
              y = ys - cellHeight/2;
            }
            else // super classes
            {
              ys = (baseRows-1)*(cellHeight+labelVertSpacing)+
                di->yPos()*(cellHeight+labelVertSpacing)/gridHeight;
              y = ys + cellHeight/2;
            }
          }
          else
          {
            xf = di->parentItem()->xPos()/(float)gridWidth;
            if (doBase)
            {
              ysf = di->yPos()/(float)gridHeight+superRows-1;
              yf = ysf + 0.5f;
            }
            else
            {
              ysf = (float)superRows-0.25f-di->yPos()/(float)gridHeight;
              yf = ysf - 0.25f;
            }
          }
          while (di!=last) // more children to add
          {
            if (bitmap)
            {
              if (doBase) // base classes
              {
                image->drawHorzArrow(y,x,x+cellWidth/2+labelHorSpacing,
                    protToColor(di->protection()),
                    protToMask(di->protection()));
                y -= cellHeight+labelVertSpacing;
              }
              else // super classes
              {
                image->drawHorzLine(y,x,x+cellWidth/2+labelHorSpacing,
                    protToColor(di->protection()),
                    protToMask(di->protection()));
                y += cellHeight+labelVertSpacing;
              }
            }
            else
            {
              t << protToString(di->protection()) << "\n";
              if (doBase)
              {
                t << "1 " << xf << " " << yf << " hedge\n";
                yf += 1.0f;
              }
              else
              {
                t << "0 " << xf << " " << yf << " hedge\n";
                yf -= 1.0f;
              }
            }
            ++rit;
            if (rit!=dr->end()) di = (*rit).get(); else di=0;
          }
          // add last horizontal line and a vertical connection line
          if (bitmap)
          {
            if (doBase) // base classes
            {
              image->drawHorzArrow(y,x,x+cellWidth/2+labelHorSpacing,
                  protToColor(di->protection()),
                  protToMask(di->protection()));
              image->drawVertLine(x,y,ys+labelVertSpacing/2,
                  protToColor(getMinProtectionLevel(dil)),
                  protToMask(getMinProtectionLevel(dil)));
            }
            else // super classes
            {
              image->drawHorzLine(y,x,x+cellWidth/2+labelHorSpacing,
                  protToColor(di->protection()),
                  protToMask(di->protection()));
              image->drawVertLine(x,ys-labelVertSpacing/2,y,
                  protToColor(getMinProtectionLevel(dil)),
                  protToMask(getMinProtectionLevel(dil)));
            }
          }
          else
          {
            t << protToString(di->protection()) << "\n";
            if (doBase)
            {
              t << "1 " << xf << " " << yf << " hedge\n";
            }
            else
            {
              t << "0 " << xf << " " << yf << " hedge\n";
            }
            t << protToString(getMinProtectionLevel(dil)) << "\n";
            if (doBase)
            {
              t << xf << " " << ysf << " " << yf << " vedge\n";
            }
            else
            {
              t << xf << " " << (ysf + 0.25f) << " " << yf << " vedge\n";
            }
          }
        }
        if (rit!=dr->end()) ++rit;
      }
      done=TRUE; // the tree is drawn now
    }
    else // normal tree connector
    {
      for (const auto &di : *dr)
      {
        uint x=0,y=0;
        DiagramItemList dil = di->getChildren();
        DiagramItem *parent  = di->parentItem();
        if (parent) // item has a parent -> connect to it
        {
          if (bitmap) // draw pixels
          {
            x = di->xPos()*(cellWidth+labelHorSpacing)/gridWidth + cellWidth/2;
            if (doBase) // base classes
            {
              y = image->height()-
                (superRows-1)*(cellHeight+labelVertSpacing)-
                di->yPos()*(cellHeight+labelVertSpacing)/gridHeight;
              /* write input line */
              image->drawVertArrow(x,y,y+labelVertSpacing/2,
                  protToColor(di->protection()),
                  protToMask(di->protection()));
            }
            else // super classes
            {
              y = (baseRows-1)*(cellHeight+labelVertSpacing)-
                labelVertSpacing/2+
                di->yPos()*(cellHeight+labelVertSpacing)/gridHeight;
              /* write output line */
              image->drawVertLine(x,y,y+labelVertSpacing/2,
                  protToColor(di->protection()),
                  protToMask(di->protection()));
            }
          }
          else // draw pixels
          {
            t << protToString(di->protection()) << "\n";
            if (doBase)
            {
              t << "1 " << di->xPos()/(float)gridWidth << " "
                << (di->yPos()/(float)gridHeight+superRows-1) << " in\n";
            }
            else
            {
              t << "0 " << di->xPos()/(float)gridWidth << " "
                << ((float)superRows-0.25f-di->yPos()/(float)gridHeight)
                << " in\n";
            }
          }
        }
        if (!dil.empty())
        {
          Protection p=getMinProtectionLevel(dil);
          uint mask=protToMask(p);
          uchar col=protToColor(p);
          if (bitmap)
          {
            x = di->xPos()*(cellWidth+labelHorSpacing)/gridWidth + cellWidth/2;
            if (doBase) // base classes
            {
              y = image->height()-
                (superRows-1)*(cellHeight+labelVertSpacing)-
                cellHeight-labelVertSpacing/2-
                di->yPos()*(cellHeight+labelVertSpacing)/gridHeight;
              image->drawVertLine(x,y,y+labelVertSpacing/2-1,col,mask);
            }
            else // super classes
            {
              y = (baseRows-1)*(cellHeight+labelVertSpacing)+
                cellHeight+
                di->yPos()*(cellHeight+labelVertSpacing)/gridHeight;
              image->drawVertArrow(x,y,y+labelVertSpacing/2-1,col,mask);
            }
          }
          else
          {
            t << protToString(p) << "\n";
            if (doBase)
            {
              t << "0 " << di->xPos()/(float)gridWidth  << " "
                << (di->yPos()/(float)gridHeight+superRows-1) << " out\n";
            }
            else
            {
              t << "1 " << di->xPos()/(float)gridWidth  << " "
                << ((float)superRows-1.75f-di->yPos()/(float)gridHeight)
                << " out\n";
            }
          }
          /* write input line */
          DiagramItem *first = dil.front();
          DiagramItem *last  = dil.back();
          if (first!=last && !first->isInList()) /* connect with all base classes */
          {
            if (bitmap)
            {
              uint xs = first->xPos()*(cellWidth+labelHorSpacing)/gridWidth
                + cellWidth/2;
              uint xe = last->xPos()*(cellWidth+labelHorSpacing)/gridWidth
                + cellWidth/2;
              if (doBase) // base classes
              {
                image->drawHorzLine(y,xs,xe,col,mask);
              }
              else // super classes
              {
                image->drawHorzLine(y+labelVertSpacing/2,xs,xe,col,mask);
              }
            }
            else
            {
              t << protToString(p) << "\n";
              if (doBase)
              {
                t << first->xPos()/(float)gridWidth << " "
                  << last->xPos()/(float)gridWidth << " "
                  << (first->yPos()/(float)gridHeight+superRows-1)
                  << " conn\n";
              }
              else
              {
                t << first->xPos()/(float)gridWidth << " "
                  << last->xPos()/(float)gridWidth << " "
                  << ((float)superRows-first->yPos()/(float)gridHeight)
                  << " conn\n";
              }
            }
          }
        }
      }
    }
  }
}

//-----------------------------------------------------------------

struct ClassDiagram::Private
{
  Private(const ClassDef *root) : base(root,true), super(root,false) {}
  TreeDiagram base;
  TreeDiagram super;
};

//-----------------------------------------------------------------


ClassDiagram::ClassDiagram(const ClassDef *root) : p(std::make_unique<Private>(root))
{
  p->base.computeLayout();
  p->super.computeLayout();
  DiagramItem *baseItem  = p->base.row(0)->item(0);
  DiagramItem *superItem = p->super.row(0)->item(0);
  uint xbase  = baseItem->xPos();
  uint xsuper = superItem->xPos();
  if (xbase>xsuper)
  {
    superItem->move((int)(xbase-xsuper),0);
    p->super.moveChildren(superItem,(int)(xbase-xsuper));
  }
  else if (xbase<xsuper)
  {
    baseItem->move((int)(xsuper-xbase),0);
    p->base.moveChildren(baseItem,(int)(xsuper-xbase));
  }
}

ClassDiagram::~ClassDiagram()
{
}

void ClassDiagram::writeFigure(TextStream &output,const QCString &path,
                               const QCString &fileName) const
{
  uint baseRows=p->base.computeRows();
  uint superRows=p->super.computeRows();
  uint baseMaxX, baseMaxLabelWidth, superMaxX, superMaxLabelWidth;
  p->base.computeExtremes(&baseMaxLabelWidth,&baseMaxX);
  p->super.computeExtremes(&superMaxLabelWidth,&superMaxX);

  uint rows=std::max(1u,baseRows+superRows-1);
  uint cols=(std::max(baseMaxX,superMaxX)+gridWidth*2-1)/gridWidth;

  // Estimate the image aspect width and height in pixels.
  uint estHeight = rows*40;
  uint estWidth  = cols*(20+std::max(baseMaxLabelWidth,superMaxLabelWidth));
  //printf("Estimated size %d x %d\n",estWidth,estHeight);

  const float pageWidth = 14.0f; // estimated page width in cm.
                                 // Somewhat lower to deal with estimation
                                 // errors.

  // compute the image height in centimeters based on the estimates
  float realHeight = static_cast<float>(std::min(rows,12u)); // real height in cm
  float realWidth  = realHeight * estWidth/static_cast<float>(estHeight);
  if (realWidth>pageWidth) // assume that the page width is about 15 cm
  {
    realHeight*=pageWidth/realWidth;
  }

  //output << "}\n";
  output << "\\begin{figure}[H]\n"
            "\\begin{center}\n"
            "\\leavevmode\n";
  output << "\\includegraphics[height=" << realHeight << "cm]{"
                                        << fileName << "}\n";
  output << "\\end{center}\n"
            "\\end{figure}\n";

  //printf("writeFigure rows=%d cols=%d\n",rows,cols);

  QCString epsBaseName=(QCString)path+"/"+fileName;
  QCString epsName=epsBaseName+".eps";
  std::ofstream f(epsName.str(),std::ofstream::out | std::ofstream::binary);
  if (!f.is_open())
  {
    term("Could not open file %s for writing\n",qPrint(epsName));
  }
  else
  {
    TextStream t(&f);

    //printf("writeEPS() rows=%d cols=%d\n",rows,cols);

    // generate EPS header and postscript variables and procedures

    t << "%!PS-Adobe-2.0 EPSF-2.0\n";
    t << "%%Title: ClassName\n";
    t << "%%Creator: Doxygen\n";
    t << "%%CreationDate: Time\n";
    t << "%%For: \n";
    t << "%Magnification: 1.00\n";
    t << "%%Orientation: Portrait\n";
    t << "%%BoundingBox: 0 0 500 " << estHeight*500.0f/(float)estWidth << "\n";
    t << "%%Pages: 0\n";
    t << "%%BeginSetup\n";
    t << "%%EndSetup\n";
    t << "%%EndComments\n";
    t << "\n";
    t << "% ----- variables -----\n";
    t << "\n";
    t << "/boxwidth 0 def\n";
    t << "/boxheight 40 def\n";
    t << "/fontheight 24 def\n";
    t << "/marginwidth 10 def\n";
    t << "/distx 20 def\n";
    t << "/disty 40 def\n";
    t << "/boundaspect " << estWidth/(float)estHeight << " def  % aspect ratio of the BoundingBox (width/height)\n";
    t << "/boundx 500 def\n";
    t << "/boundy boundx boundaspect div def\n";
    t << "/xspacing 0 def\n";
    t << "/yspacing 0 def\n";
    t << "/rows " << rows << " def\n";
    t << "/cols " << cols << " def\n";
    t << "/scalefactor 0 def\n";
    t << "/boxfont /Times-Roman findfont fontheight scalefont def\n";
    t << "\n";
    t << "% ----- procedures -----\n";
    t << "\n";
    t << "/dotted { [1 4] 0 setdash } def\n";
    t << "/dashed { [5] 0 setdash } def\n";
    t << "/solid  { [] 0 setdash } def\n";
    t << "\n";
    t << "/max % result = MAX(arg1,arg2)\n";
    t << "{\n";
    t << "  /a exch def\n";
    t << "  /b exch def\n";
    t << "  a b gt {a} {b} ifelse\n";
    t << "} def\n";
    t << "\n";
    t << "/xoffset % result = MAX(0,(scalefactor-(boxwidth*cols+distx*(cols-1)))/2)\n";
    t << "{\n";
    t << "  0 scalefactor boxwidth cols mul distx cols 1 sub mul add sub 2 div max\n";
    t << "} def\n";
    t << "\n";
    t << "/cw % boxwidth = MAX(boxwidth, stringwidth(arg1))\n";
    t << "{\n";
    t << "  /str exch def\n";
    t << "  /boxwidth boxwidth str stringwidth pop max def\n";
    t << "} def\n";
    t << "\n";
    t << "/box % draws a box with text 'arg1' at grid pos (arg2,arg3)\n";
    t << "{ gsave\n";
    t << "  2 setlinewidth\n";
    t << "  newpath\n";
    t << "  exch xspacing mul xoffset add\n";
    t << "  exch yspacing mul\n";
    t << "  moveto\n";
    t << "  boxwidth 0 rlineto \n";
    t << "  0 boxheight rlineto \n";
    t << "  boxwidth neg 0 rlineto \n";
    t << "  0 boxheight neg rlineto \n";
    t << "  closepath\n";
    t << "  dup stringwidth pop neg boxwidth add 2 div\n";
    t << "  boxheight fontheight 2 div sub 2 div\n";
    t << "  rmoveto show stroke\n";
    t << "  grestore\n";
    t << "} def  \n";
    t << "\n";
    t << "/mark\n";
    t << "{ newpath\n";
    t << "  exch xspacing mul xoffset add boxwidth add\n";
    t << "  exch yspacing mul\n";
    t << "  moveto\n";
    t << "  0 boxheight 4 div rlineto\n";
    t << "  boxheight neg 4 div boxheight neg 4 div rlineto\n";
    t << "  closepath\n";
    t << "  eofill\n";
    t << "  stroke\n";
    t << "} def\n";
    t << "\n";
    t << "/arrow\n";
    t << "{ newpath\n";
    t << "  moveto\n";
    t << "  3 -8 rlineto\n";
    t << "  -6 0 rlineto\n";
    t << "  3 8 rlineto\n";
    t << "  closepath\n";
    t << "  eofill\n";
    t << "  stroke\n";
    t << "} def\n";
    t << "\n";
    t << "/out % draws an output connector for the block at (arg1,arg2)\n";
    t << "{\n";
    t << "  newpath\n";
    t << "  exch xspacing mul xoffset add boxwidth 2 div add\n";
    t << "  exch yspacing mul boxheight add\n";
    t << "  /y exch def\n";
    t << "  /x exch def\n";
    t << "  x y moveto\n";
    t << "  0 disty 2 div rlineto \n";
    t << "  stroke\n";
    t << "  1 eq { x y disty 2 div add arrow } if\n";
    t << "} def\n";
    t << "\n";
    t << "/in % draws an input connector for the block at (arg1,arg2)\n";
    t << "{\n";
    t << "  newpath\n";
    t << "  exch xspacing mul xoffset add boxwidth 2 div add\n";
    t << "  exch yspacing mul disty 2 div sub\n";
    t << "  /y exch def\n";
    t << "  /x exch def\n";
    t << "  x y moveto\n";
    t << "  0 disty 2 div rlineto\n";
    t << "  stroke\n";
    t << "  1 eq { x y disty 2 div add arrow } if\n";
    t << "} def\n";
    t << "\n";
    t << "/hedge\n";
    t << "{\n";
    t << "  exch xspacing mul xoffset add boxwidth 2 div add\n";
    t << "  exch yspacing mul boxheight 2 div sub\n";
    t << "  /y exch def\n";
    t << "  /x exch def\n";
    t << "  newpath\n";
    t << "  x y moveto\n";
    t << "  boxwidth 2 div distx add 0 rlineto\n";
    t << "  stroke\n";
    t << "  1 eq\n";
    t << "  { newpath x boxwidth 2 div distx add add y moveto\n";
    t << "    -8 3 rlineto\n";
    t << "    0 -6 rlineto\n";
    t << "    8 3 rlineto\n";
    t << "    closepath\n";
    t << "    eofill\n";
    t << "    stroke\n";
    t << "  } if\n";
    t << "} def\n";
    t << "\n";
    t << "/vedge\n";
    t << "{\n";
    t << "  /ye exch def\n";
    t << "  /ys exch def\n";
    t << "  /xs exch def\n";
    t << "  newpath\n";
    t << "  xs xspacing mul xoffset add boxwidth 2 div add dup\n";
    t << "  ys yspacing mul boxheight 2 div sub\n";
    t << "  moveto\n";
    t << "  ye yspacing mul boxheight 2 div sub\n";
    t << "  lineto\n";
    t << "  stroke\n";
    t << "} def\n";
    t << "\n";
    t << "/conn % connections the blocks from col 'arg1' to 'arg2' of row 'arg3'\n";
    t << "{\n";
    t << "  /ys exch def\n";
    t << "  /xe exch def\n";
    t << "  /xs exch def\n";
    t << "  newpath\n";
    t << "  xs xspacing mul xoffset add boxwidth 2 div add\n";
    t << "  ys yspacing mul disty 2 div sub\n";
    t << "  moveto\n";
    t << "  xspacing xe xs sub mul 0\n";
    t << "  rlineto\n";
    t << "  stroke\n";
    t << "} def\n";
    t << "\n";
    t << "% ----- main ------\n";
    t << "\n";
    t << "boxfont setfont\n";
    t << "1 boundaspect scale\n";


    for (const auto &dr : p->base)
    {
      bool done=FALSE;
      for (const auto &di : *dr)
      {
        done=di->isInList();
        t << "(" << convertToPSString(di->label()) << ") cw\n";
      }
      if (done) break;
    }

    auto it = p->super.begin();
    if (it!=p->super.end()) ++it;
    for (;it!=p->super.end();++it)
    {
      const auto &dr = *it;
      bool done=FALSE;
      for (const auto &di : *dr)
      {
        done=di->isInList();
        t << "(" << convertToPSString(di->label()) << ") cw\n";
      }
      if (done) break;
    }

    t << "/boxwidth boxwidth marginwidth 2 mul add def\n"
      << "/xspacing boxwidth distx add def\n"
      << "/yspacing boxheight disty add def\n"
      << "/scalefactor \n"
      << "  boxwidth cols mul distx cols 1 sub mul add\n"
      << "  boxheight rows mul disty rows 1 sub mul add boundaspect mul \n"
      << "  max def\n"
      << "boundx scalefactor div boundy scalefactor div scale\n";

    t << "\n% ----- classes -----\n\n";
    p->base.drawBoxes(t,0,TRUE,FALSE,baseRows,superRows,0,0);
    p->super.drawBoxes(t,0,FALSE,FALSE,baseRows,superRows,0,0);

    t << "\n% ----- relations -----\n\n";
    p->base.drawConnectors(t,0,TRUE,FALSE,baseRows,superRows,0,0);
    p->super.drawConnectors(t,0,FALSE,FALSE,baseRows,superRows,0,0);

  }
  f.close();

  if (Config_getBool(USE_PDFLATEX))
  {
    QCString epstopdfArgs(4096);
    epstopdfArgs.sprintf("\"%s.eps\" --outfile=\"%s.pdf\"",
                   qPrint(epsBaseName),qPrint(epsBaseName));
    //printf("Converting eps using '%s'\n",qPrint(epstopdfArgs));
    Portable::sysTimerStart();
    if (Portable::system("epstopdf",epstopdfArgs)!=0)
    {
       err("Problems running epstopdf. Check your TeX installation!\n");
       Portable::sysTimerStop();
       return;
    }
    Portable::sysTimerStop();
  }
}


void ClassDiagram::writeImage(TextStream &t,const QCString &path,
                              const QCString &relPath,const QCString &fileName,
                              bool generateMap) const
{
  uint baseRows=p->base.computeRows();
  uint superRows=p->super.computeRows();
  uint rows=baseRows+superRows-1;

  uint lb,ls,xb,xs;
  p->base.computeExtremes(&lb,&xb);
  p->super.computeExtremes(&ls,&xs);

  uint cellWidth  = std::max(lb,ls)+labelHorMargin*2;
  uint maxXPos    = std::max(xb,xs);
  uint labelVertMargin = 6; //std::max(6,(cellWidth-fontHeight)/6); // aspect at least 1:3
  uint cellHeight = labelVertMargin*2+fontHeight;
  uint imageWidth = (maxXPos+gridWidth)*cellWidth/gridWidth+
                    (maxXPos*labelHorSpacing)/gridWidth;
  uint imageHeight = rows*cellHeight+(rows-1)*labelVertSpacing;

  Image image(imageWidth,imageHeight);

  p->base.drawBoxes(t,&image,TRUE,TRUE,baseRows,superRows,cellWidth,cellHeight,relPath,generateMap);
  p->super.drawBoxes(t,&image,FALSE,TRUE,baseRows,superRows,cellWidth,cellHeight,relPath,generateMap);
  p->base.drawConnectors(t,&image,TRUE,TRUE,baseRows,superRows,cellWidth,cellHeight);
  p->super.drawConnectors(t,&image,FALSE,TRUE,baseRows,superRows,cellWidth,cellHeight);

#define IMAGE_EXT ".png"
  image.save((QCString)path+"/"+fileName+IMAGE_EXT);
  Doxygen::indexList->addImageFile(QCString(fileName)+IMAGE_EXT);
}

