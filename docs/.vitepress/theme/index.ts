import { h } from "vue";
import DefaultTheme from "vitepress/theme";
import BabosBackground from "./components/BabosBackground.vue";
import "./babo.css";

export default {
  extends: DefaultTheme,
  // the home page's hero sits on a gasm game: two babos shooting each other (tools/site-bg)
  Layout: () => h(DefaultTheme.Layout, null, {
    "home-hero-before": () => h(BabosBackground),
  }),
};
