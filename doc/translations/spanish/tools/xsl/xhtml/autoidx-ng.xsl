<?xml version="1.0" encoding="US-ASCII"?>
<!--This file was created automatically by html2xhtml-->
<!--from the HTML stylesheets. Do not edit this file.-->
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:fo="http://www.w3.org/1999/XSL/Format" xmlns:func="http://exslt.org/functions" xmlns:i="urn:cz-kosek:functions:index" xmlns="http://www.w3.org/1999/xhtml" version="1.0" exclude-result-prefixes="i">

<!-- ********************************************************************
     $Id: autoidx-ng.xsl,v 1.1 2003/12/31 16:50:15 kosek Exp $
     ********************************************************************

     This file is part of the DocBook XSL Stylesheet distribution.
     See ../README or http://docbook.sf.net/ for copyright
     and other information.

     ******************************************************************** -->

<xsl:include href="../common/autoidx-ng.xsl"/>

<!-- Modified original code is using index group codes instead of just first letter
     to gain better grouping -->
<xsl:template name="generate-index">
  <xsl:param name="scope" select="(ancestor::book|/)[last()]"/>

  <xsl:variable name="terms" select="//indexterm[count(.|key('group-code',                                                 i:group-index(normalize-space(concat(primary/@sortas, primary[not(@sortas)]))))[count(ancestor::node()|$scope) = count(ancestor::node())][1]) = 1                                     and not(@class = 'endofrange')]"/>

  <div class="index">
    <xsl:apply-templates select="$terms" mode="index-div">
      <xsl:with-param name="scope" select="$scope"/>
      <xsl:sort select="i:group-index(normalize-space(concat(primary/@sortas, primary[not(@sortas)])))" data-type="number"/>
    </xsl:apply-templates>
  </div>
</xsl:template>

<xsl:template match="indexterm" mode="index-div">
  <xsl:param name="scope" select="."/>

  <xsl:variable name="key" select="i:group-index(normalize-space(concat(primary/@sortas, primary[not(@sortas)])))"/>

  <xsl:if test="key('group-code', $key)[count(ancestor::node()|$scope) = count(ancestor::node())]                 [count(.|key('primary', normalize-space(concat(primary/@sortas, primary[not(@sortas)])))[count(ancestor::node()|$scope) = count(ancestor::node())][1]) = 1]">
    <div class="indexdiv">
      <h3>
        <xsl:value-of select="i:group-letter($key)"/>
      </h3>
      <dl>
        <xsl:apply-templates select="key('group-code', $key)[count(ancestor::node()|$scope) = count(ancestor::node())]                                      [count(.|key('primary', normalize-space(concat(primary/@sortas, primary[not(@sortas)])))[count(ancestor::node()|$scope) = count(ancestor::node())][1])=1]" mode="index-primary">
          <xsl:sort select="translate(normalize-space(concat(primary/@sortas, primary[not(@sortas)])), 'abcdefghijklmnopqrstuvwxyz', 'ABCDEFGHIJKLMNOPQRSTUVWXYZ')"/>
          <xsl:with-param name="scope" select="$scope"/>
        </xsl:apply-templates>
      </dl>
    </div>
  </xsl:if>
</xsl:template>

</xsl:stylesheet>
