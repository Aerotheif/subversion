<?xml version='1.0'?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:fo="http://www.w3.org/1999/XSL/Format"
                xmlns:sverb="http://nwalsh.com/xslt/ext/com.nwalsh.saxon.Verbatim"
                xmlns:xverb="com.nwalsh.xalan.Verbatim"
                xmlns:lxslt="http://xml.apache.org/xslt"
                exclude-result-prefixes="sverb xverb lxslt"
                version='1.0'>

<!-- ********************************************************************
     $Id: verbatim.xsl,v 1.12 2004/02/26 17:50:19 nwalsh Exp $
     ********************************************************************

     This file is part of the XSL DocBook Stylesheet distribution.
     See ../README or http://nwalsh.com/docbook/xsl/ for copyright
     and other information.

     ******************************************************************** -->

<lxslt:component prefix="xverb"
                 functions="numberLines"/>

<xsl:template match="programlisting|screen|synopsis">
  <xsl:param name="suppress-numbers" select="'0'"/>
  <xsl:variable name="id"><xsl:call-template name="object.id"/></xsl:variable>

  <xsl:variable name="content">
    <xsl:choose>
      <xsl:when test="$suppress-numbers = '0'
                      and @linenumbering = 'numbered'
                      and $use.extensions != '0'
                      and $linenumbering.extension != '0'">
        <xsl:call-template name="number.rtf.lines">
          <xsl:with-param name="rtf">
            <xsl:apply-templates/>
          </xsl:with-param>
        </xsl:call-template>
      </xsl:when>
      <xsl:otherwise>
        <xsl:apply-templates/>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:variable>

  <xsl:choose>
    <xsl:when test="$shade.verbatim != 0">
      <fo:block wrap-option='no-wrap'
                white-space-collapse='false'
		white-space-treatment='preserve'
                linefeed-treatment='preserve'
                xsl:use-attribute-sets="monospace.verbatim.properties shade.verbatim.style">

        <xsl:copy-of select="$content"/>
      </fo:block>
    </xsl:when>
    <xsl:otherwise>
      <fo:block wrap-option='no-wrap'
                white-space-collapse='false'
		white-space-treatment='preserve'
                linefeed-treatment="preserve"
                xsl:use-attribute-sets="monospace.verbatim.properties">
        <xsl:copy-of select="$content"/>
      </fo:block>
    </xsl:otherwise>
  </xsl:choose>
</xsl:template>

<xsl:template match="literallayout">
  <xsl:param name="suppress-numbers" select="'0'"/>

  <xsl:variable name="content">
    <xsl:choose>
      <xsl:when test="$suppress-numbers = '0'
                      and @linenumbering = 'numbered'
                      and $use.extensions != '0'
                      and $linenumbering.extension != '0'">
        <xsl:call-template name="number.rtf.lines">
          <xsl:with-param name="rtf">
            <xsl:apply-templates/>
          </xsl:with-param>
        </xsl:call-template>
      </xsl:when>
      <xsl:otherwise>
        <xsl:apply-templates/>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:variable>

  <xsl:choose>
    <xsl:when test="@class='monospaced'">
      <xsl:choose>
        <xsl:when test="$shade.verbatim != 0">
          <fo:block wrap-option='no-wrap'
                    white-space-collapse='false'
		    white-space-treatment='preserve'
                    linefeed-treatment="preserve"
                    xsl:use-attribute-sets="monospace.verbatim.properties shade.verbatim.style">

            <xsl:copy-of select="$content"/>
          </fo:block>
        </xsl:when>
        <xsl:otherwise>
          <fo:block wrap-option='no-wrap'
                    white-space-collapse='false'
		    white-space-treatment='preserve'
                    linefeed-treatment="preserve"
                    xsl:use-attribute-sets="monospace.verbatim.properties">
            <xsl:copy-of select="$content"/>
          </fo:block>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:when>
    <xsl:otherwise>
      <xsl:choose>
        <xsl:when test="$shade.verbatim != 0">
          <fo:block wrap-option='no-wrap'
                    white-space-collapse='false'
		    white-space-treatment='preserve'
                    text-align='start'
                    linefeed-treatment="preserve"
                    xsl:use-attribute-sets="verbatim.properties shade.verbatim.style">
            <xsl:copy-of select="$content"/>
          </fo:block>
        </xsl:when>
        <xsl:otherwise>
          <fo:block wrap-option='no-wrap'
                    white-space-collapse='false'
		    white-space-treatment='preserve'
                    text-align='start'
                    linefeed-treatment="preserve"
                    xsl:use-attribute-sets="verbatim.properties">
            <xsl:copy-of select="$content"/>
          </fo:block>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:otherwise>
  </xsl:choose>
</xsl:template>

<xsl:template match="address">
  <xsl:param name="suppress-numbers" select="'0'"/>

  <xsl:variable name="content">
    <xsl:choose>
      <xsl:when test="$suppress-numbers = '0'
                      and @linenumbering = 'numbered'
                      and $use.extensions != '0'
                      and $linenumbering.extension != '0'">
        <xsl:call-template name="number.rtf.lines">
          <xsl:with-param name="rtf">
            <xsl:apply-templates/>
          </xsl:with-param>
        </xsl:call-template>
      </xsl:when>
      <xsl:otherwise>
        <xsl:apply-templates/>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:variable>

  <fo:block wrap-option='no-wrap'
            white-space-collapse='false'
	    white-space-treatment='preserve'
            linefeed-treatment="preserve"
            xsl:use-attribute-sets="verbatim.properties">
    <xsl:copy-of select="$content"/>
  </fo:block>
</xsl:template>

<xsl:template name="number.rtf.lines">
  <xsl:param name="rtf" select="''"/>
  <xsl:param name="pi.context" select="."/>

  <!-- Save the global values -->
  <xsl:variable name="global.linenumbering.everyNth"
                select="$linenumbering.everyNth"/>

  <xsl:variable name="global.linenumbering.separator"
                select="$linenumbering.separator"/>

  <xsl:variable name="global.linenumbering.width"
                select="$linenumbering.width"/>

  <!-- Extract the <?dbfo linenumbering.*?> PI values -->
  <xsl:variable name="pi.linenumbering.everyNth">
    <xsl:call-template name="dbfo-attribute">
      <xsl:with-param name="pis"
                      select="$pi.context/processing-instruction('dbfo')"/>
      <xsl:with-param name="attribute" select="'linenumbering.everyNth'"/>
    </xsl:call-template>
  </xsl:variable>

  <xsl:variable name="pi.linenumbering.separator">
    <xsl:call-template name="dbfo-attribute">
      <xsl:with-param name="pis"
                      select="$pi.context/processing-instruction('dbfo')"/>
      <xsl:with-param name="attribute" select="'linenumbering.separator'"/>
    </xsl:call-template>
  </xsl:variable>

  <xsl:variable name="pi.linenumbering.width">
    <xsl:call-template name="dbfo-attribute">
      <xsl:with-param name="pis"
                      select="$pi.context/processing-instruction('dbfo')"/>
      <xsl:with-param name="attribute" select="'linenumbering.width'"/>
    </xsl:call-template>
  </xsl:variable>

  <!-- Construct the 'in-context' values -->
  <xsl:variable name="linenumbering.everyNth">
    <xsl:choose>
      <xsl:when test="$pi.linenumbering.everyNth != ''">
        <xsl:value-of select="$pi.linenumbering.everyNth"/>
      </xsl:when>
      <xsl:otherwise>
        <xsl:value-of select="$global.linenumbering.everyNth"/>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:variable>

  <xsl:variable name="linenumbering.separator">
    <xsl:choose>
      <xsl:when test="$pi.linenumbering.separator != ''">
        <xsl:value-of select="$pi.linenumbering.separator"/>
      </xsl:when>
      <xsl:otherwise>
        <xsl:value-of select="$global.linenumbering.separator"/>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:variable>

  <xsl:variable name="linenumbering.width">
    <xsl:choose>
      <xsl:when test="$pi.linenumbering.width != ''">
        <xsl:value-of select="$pi.linenumbering.width"/>
      </xsl:when>
      <xsl:otherwise>
        <xsl:value-of select="$global.linenumbering.width"/>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:variable>

  <xsl:variable name="linenumbering.startinglinenumber">
    <xsl:choose>
      <xsl:when test="@startinglinenumber">
        <xsl:value-of select="@startinglinenumber"/>
      </xsl:when>
      <xsl:when test="@continuation='continues'">
        <xsl:variable name="lastLine">
          <xsl:choose>
            <xsl:when test="self::programlisting">
              <xsl:call-template name="lastLineNumber">
                <xsl:with-param name="listings"
                     select="preceding::programlisting[@linenumbering='numbered']"/>
              </xsl:call-template>
            </xsl:when>
            <xsl:when test="self::screen">
              <xsl:call-template name="lastLineNumber">
                <xsl:with-param name="listings"
                     select="preceding::screen[@linenumbering='numbered']"/>
              </xsl:call-template>
            </xsl:when>
            <xsl:when test="self::literallayout">
              <xsl:call-template name="lastLineNumber">
                <xsl:with-param name="listings"
                     select="preceding::literallayout[@linenumbering='numbered']"/>
              </xsl:call-template>
            </xsl:when>
            <xsl:when test="self::address">
              <xsl:call-template name="lastLineNumber">
                <xsl:with-param name="listings"
                     select="preceding::address[@linenumbering='numbered']"/>
              </xsl:call-template>
            </xsl:when>
            <xsl:when test="self::synopsis">
              <xsl:call-template name="lastLineNumber">
                <xsl:with-param name="listings"
                     select="preceding::synopsis[@linenumbering='numbered']"/>
              </xsl:call-template>
            </xsl:when>
            <xsl:otherwise>
              <xsl:message>
                <xsl:text>Unexpected verbatim environment: </xsl:text>
                <xsl:value-of select="local-name(.)"/>
              </xsl:message>
              <xsl:value-of select="0"/>
            </xsl:otherwise>
          </xsl:choose>
        </xsl:variable>

        <xsl:value-of select="$lastLine + 1"/>
      </xsl:when>
      <xsl:otherwise>1</xsl:otherwise>
    </xsl:choose>
  </xsl:variable>

  <xsl:choose>
    <xsl:when test="function-available('sverb:numberLines')">
      <xsl:copy-of select="sverb:numberLines($rtf)"/>
    </xsl:when>
    <xsl:when test="function-available('xverb:numberLines')">
      <xsl:copy-of select="xverb:numberLines($rtf)"/>
    </xsl:when>
    <xsl:otherwise>
      <xsl:message terminate="yes">
        <xsl:text>No numberLines function available.</xsl:text>
      </xsl:message>
    </xsl:otherwise>
  </xsl:choose>
</xsl:template>

<!-- ======================================================================== -->

<xsl:template name="lastLineNumber">
  <xsl:param name="listings"/>
  <xsl:param name="number" select="0"/>

  <xsl:variable name="lines">
    <xsl:call-template name="countLines">
      <xsl:with-param name="listing" select="string($listings[1])"/>
    </xsl:call-template>
  </xsl:variable>

  <xsl:choose>
    <xsl:when test="not($listings)">
      <xsl:value-of select="$number"/>
    </xsl:when>
    <xsl:when test="$listings[1]/@startinglinenumber">
      <xsl:value-of select="$number + $listings[1]/@startinglinenumber + $lines - 1"/>
    </xsl:when>
    <xsl:when test="$listings[1]/@continuation='continues'">
      <xsl:call-template name="lastLineNumber">
        <xsl:with-param name="listings" select="listings[position() &gt; 1]"/>
        <xsl:with-param name="number" select="$number + $lines"/>
      </xsl:call-template>
    </xsl:when>
    <xsl:otherwise>
      <xsl:value-of select="$lines"/>
    </xsl:otherwise>
  </xsl:choose>
</xsl:template>

<xsl:template name="countLines">
  <xsl:param name="listing"/>
  <xsl:param name="count" select="1"/>

  <xsl:choose>
    <xsl:when test="contains($listing, '&#10;')">
      <xsl:call-template name="countLines">
        <xsl:with-param name="listing" select="substring-after($listing, '&#10;')"/>
        <xsl:with-param name="count" select="$count + 1"/>
      </xsl:call-template>
    </xsl:when>
    <xsl:otherwise>
      <xsl:value-of select="$count"/>
    </xsl:otherwise>
  </xsl:choose>
</xsl:template>

</xsl:stylesheet>
