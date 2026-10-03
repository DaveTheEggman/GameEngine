<screen mode="overlay">

  <Frame width="match" height="match" padding="18">

    <Panel gravity="Top|Left" padding="16"
           style="background: rounded-rect(rgb(20, 22, 28), radius=8, border-width=1, border=rgb(70, 78, 94));">

      <Flex direction="horizontal" align="center" spacing="30">
        <Flex direction="vertical" align="center">
          <Label text="TIME" font-size="16" style="text-color: rgb(190, 198, 214);"/>
          <Label id="hud-time" text="0" font-size="34" style="text-color: rgb(255, 214, 102);"/>
        </Flex>
        <Flex direction="vertical" align="center">
          <Label text="DELIVERED" font-size="16" style="text-color: rgb(190, 198, 214);"/>
          <Label id="hud-deliveries" text="0 / 0" font-size="34" class="label"/>
        </Flex>
        <Flex direction="vertical" align="center">
          <Label text="PAPERS" font-size="16" style="text-color: rgb(190, 198, 214);"/>
          <Label id="hud-papers" text="0" font-size="34" class="label"/>
        </Flex>
        <Flex direction="vertical" align="center">
          <Label text="SCORE" font-size="16" style="text-color: rgb(190, 198, 214);"/>
          <Label id="hud-score" text="0" font-size="34" class="label"/>
        </Flex>
        <Flex direction="vertical" align="center">
          <Label text="LIVES" font-size="16" style="text-color: rgb(190, 198, 214);"/>
          <Label id="hud-lives" text="3" font-size="34" style="text-color: rgb(240, 110, 100);"/>
        </Flex>
      </Flex>

    </Panel>

    <!-- A delivery's points, rising from under the score as they fade (PaperKidGame.as moves it). -->
    <Label id="hud-pop" gravity="Top|Left" text="+100" font-family="Lilita One" font-size="30" visibility="hidden"
           style="text-color: rgb(255, 214, 102);"/>

    <!-- A cleared block's banner, dropping in over the slowing street (PaperKidGame.as). -->
    <Label id="hud-banner" gravity="Center" text="Block cleared!" font-family="Lilita One" font-size="84"
           visibility="hidden" style="text-color: rgb(255, 214, 102);"/>

    <!-- The minimap: the Minimap render texture the block's top-down camera draws, with markers
         over it that Minimap.as places from world positions (translations from the map's corner). -->
    <Panel gravity="Top|Right" padding="6"
           style="background: rounded-rect(rgb(20, 22, 28), radius=8, border-width=1, border=rgb(70, 78, 94));">
      <Frame id="map" width="200" height="200">
        <ImageView id="map-image" width="200" height="200" source="{abeaf29a-4112-4ffc-9d05-b84450c24b57}"/>
        <Panel id="map-sub-0" width="12" height="12" visibility="hidden"
               style="background: rounded-rect(rgb(255, 214, 102), radius=6, border-width=2, border=rgb(20, 22, 28));"/>
        <Panel id="map-sub-1" width="12" height="12" visibility="hidden"
               style="background: rounded-rect(rgb(255, 214, 102), radius=6, border-width=2, border=rgb(20, 22, 28));"/>
        <Panel id="map-sub-2" width="12" height="12" visibility="hidden"
               style="background: rounded-rect(rgb(255, 214, 102), radius=6, border-width=2, border=rgb(20, 22, 28));"/>
        <Panel id="map-sub-3" width="12" height="12" visibility="hidden"
               style="background: rounded-rect(rgb(255, 214, 102), radius=6, border-width=2, border=rgb(20, 22, 28));"/>
        <Panel id="map-sub-4" width="12" height="12" visibility="hidden"
               style="background: rounded-rect(rgb(255, 214, 102), radius=6, border-width=2, border=rgb(20, 22, 28));"/>
        <Panel id="map-sub-5" width="12" height="12" visibility="hidden"
               style="background: rounded-rect(rgb(255, 214, 102), radius=6, border-width=2, border=rgb(20, 22, 28));"/>
        <Panel id="map-sub-6" width="12" height="12" visibility="hidden"
               style="background: rounded-rect(rgb(255, 214, 102), radius=6, border-width=2, border=rgb(20, 22, 28));"/>
        <Panel id="map-sub-7" width="12" height="12" visibility="hidden"
               style="background: rounded-rect(rgb(255, 214, 102), radius=6, border-width=2, border=rgb(20, 22, 28));"/>
        <!-- The bike: a long body with a white nose, turned to its heading. -->
        <Frame id="map-bike" width="10" height="16" visibility="hidden">
          <Panel width="10" height="16"
                 style="background: rounded-rect(rgb(240, 70, 60), radius=3, border-width=1, border=rgb(20, 22, 28));"/>
          <Panel gravity="Top|CenterH" width="6" height="5" margin="1"
                 style="background: rounded-rect(rgb(255, 255, 255), radius=2);"/>
        </Frame>
      </Frame>
    </Panel>

  </Frame>

</screen>
